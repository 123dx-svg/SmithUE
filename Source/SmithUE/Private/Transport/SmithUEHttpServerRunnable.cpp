#include "Transport/SmithUEHttpServerRunnable.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Transport/SmithUEHttpServer.h"
#include "SmithUEModule.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "Utils/SmithUEDispatcher.h"
#include "Commands/SmithUEDialogCommands.h"
#include "Utils/SmithUEProgress.h"
#include "Misc/DateTime.h"
#include "Dialog/SmithUEDialogWatcher.h"

namespace SmithUEHttpServer::Private
{
	constexpr int32 ReceiveBufferSize = 8 * 1024;
	using FParsedHttpRequest = FSmithUEHttpServerRunnable::FParsedHttpRequest;

	enum class EHttpRequestReceiveResult
	{
		Success,
		ParseError,
		PayloadTooLarge,
	};

	int32 FindHeaderTerminator(const TArray<uint8>& Data)
	{
		for (int32 Index = 0; Index <= Data.Num() - 4; ++Index)
		{
			if (Data[Index] == '\r' && Data[Index + 1] == '\n' && Data[Index + 2] == '\r' && Data[Index + 3] == '\n')
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	FString BytesToString(const uint8* Data, const int32 Length)
	{
		TArray<ANSICHAR> NullTerminated;
		NullTerminated.SetNumUninitialized(Length + 1);
		if (Length > 0)
		{
			FMemory::Memcpy(NullTerminated.GetData(), Data, Length);
		}
		NullTerminated[Length] = '\0';
		return UTF8_TO_TCHAR(NullTerminated.GetData());
	}

	bool ParseRequestHead(const FString& HeadText, FParsedHttpRequest& OutRequest)
	{
		TArray<FString> Lines;
		HeadText.ParseIntoArray(Lines, TEXT("\r\n"), true);
		if (Lines.Num() == 0)
		{
			return false;
		}

		TArray<FString> RequestLineParts;
		Lines[0].ParseIntoArray(RequestLineParts, TEXT(" "), true);
		if (RequestLineParts.Num() < 3)
		{
			return false;
		}

		OutRequest.Method = RequestLineParts[0];
		OutRequest.Path = RequestLineParts[1];
		OutRequest.Protocol = RequestLineParts[2];

		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			const FString& Line = Lines[Index];
			FString Key;
			FString Value;
			if (!Line.Split(TEXT(":"), &Key, &Value))
			{
				continue;
			}

			Key.TrimStartAndEndInline();
			Key.ToLowerInline();
			Value.TrimStartAndEndInline();
			OutRequest.Headers.Add(Key, Value);
		}

		if (const FString* ContentLengthValue = OutRequest.Headers.Find(TEXT("content-length")))
		{
			OutRequest.ContentLength = FMath::Max(FCString::Atoi64(**ContentLengthValue), 0LL);
		}

		return true;
	}

	EHttpRequestReceiveResult ReceiveHttpRequest(FSocket& ClientSocket, const FThreadSafeBool& bStopping, FParsedHttpRequest& OutRequest)
	{
		TArray<uint8> ReceivedData;
		TArray<uint8> Chunk;
		Chunk.SetNumUninitialized(ReceiveBufferSize);

		int32 HeaderEndIndex = INDEX_NONE;
		int64 ExpectedTotalBytes = INDEX_NONE;
		const double TimeoutSeconds = 30.0;
		const double StartTime = FPlatformTime::Seconds();
		constexpr int64 MaxBodyBytes = 10LL * 1024LL * 1024LL;

		while (!bStopping)
		{
			if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
			{
				return EHttpRequestReceiveResult::ParseError;
			}

			int32 BytesRead = 0;
			if (!ClientSocket.Recv(Chunk.GetData(), Chunk.Num(), BytesRead))
			{
				const ESocketErrors LastError = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
				if (LastError == SE_EWOULDBLOCK || LastError == SE_EINTR)
				{
					FPlatformProcess::Sleep(0.01f);
					continue;
				}
				return EHttpRequestReceiveResult::ParseError;
			}

			if (BytesRead <= 0)
			{
				// On non-blocking sockets, BytesRead==0 may mean "no data yet" rather than "closed"
				// If we're still expecting more data (body after 100-continue), keep waiting
			if (ExpectedTotalBytes != INDEX_NONE && static_cast<int64>(ReceivedData.Num()) < ExpectedTotalBytes)
				{
					FPlatformProcess::Sleep(0.01f);
					continue;
				}
				return EHttpRequestReceiveResult::ParseError;
			}

			ReceivedData.Append(Chunk.GetData(), BytesRead);

			if (HeaderEndIndex == INDEX_NONE)
			{
				HeaderEndIndex = FindHeaderTerminator(ReceivedData);
				if (HeaderEndIndex != INDEX_NONE)
				{
					const FString HeadText = BytesToString(ReceivedData.GetData(), HeaderEndIndex);
					if (!ParseRequestHead(HeadText, OutRequest))
					{
						return EHttpRequestReceiveResult::ParseError;
					}
					if (OutRequest.ContentLength > MaxBodyBytes)
					{
						return EHttpRequestReceiveResult::PayloadTooLarge;
					}
					// Handle Expect: 100-continue (PowerShell, curl etc.)
					if (const FString* ExpectValue = OutRequest.Headers.Find(TEXT("expect")))
					{
						if (ExpectValue->Contains(TEXT("100-continue")))
						{
							const FString ContinueResponse = TEXT("HTTP/1.1 100 Continue\r\n\r\n");
							FTCHARToUTF8 Utf8Continue(*ContinueResponse);
							int32 Sent = 0;
							ClientSocket.Send(reinterpret_cast<const uint8*>(Utf8Continue.Get()), Utf8Continue.Length(), Sent);
						}
					}
					ExpectedTotalBytes = HeaderEndIndex + 4 + OutRequest.ContentLength;
				}
			}

			if (ExpectedTotalBytes != INDEX_NONE && static_cast<int64>(ReceivedData.Num()) >= ExpectedTotalBytes)
			{
				if (OutRequest.ContentLength > 0)
				{
					OutRequest.Body = BytesToString(ReceivedData.GetData() + HeaderEndIndex + 4, OutRequest.ContentLength);
				}
				return EHttpRequestReceiveResult::Success;
			}
		}

		return EHttpRequestReceiveResult::ParseError;
	}

	FString BuildHttpResponse(const int32 StatusCode, const FString& StatusText, const FString& Body)
	{
		FTCHARToUTF8 Utf8Body(*Body);
		return FString::Printf(
			TEXT("HTTP/1.1 %d %s\r\n")
			TEXT("Content-Type: application/json; charset=utf-8\r\n")
			TEXT("Content-Length: %d\r\n")
			TEXT("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n")
			TEXT("Access-Control-Allow-Headers: Content-Type\r\n")
			TEXT("Connection: close\r\n")
			TEXT("\r\n")
			TEXT("%s"),
			StatusCode,
			*StatusText,
			Utf8Body.Length(),
			*Body);
	}

	FString BuildNoContentResponse()
	{
		return TEXT("HTTP/1.1 204 No Content\r\n"
			"Content-Length: 0\r\n"
			"Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
			"Access-Control-Allow-Headers: Content-Type\r\n"
			"Connection: close\r\n"
			"\r\n");
	}

	FString MakeErrorBody(const FString& Message, const FString& ErrorCode = TEXT("INTERNAL_ERROR"))
	{
		return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateErrorResponse(Message, ErrorCode));
	}

	uint16 GetPortFromCommandLine(const TCHAR* FlagName, uint16 DefaultPort)
	{
		int32 RequestedPort = 0;
		if (!FParse::Value(FCommandLine::Get(), FlagName, RequestedPort) || RequestedPort <= 0 || RequestedPort > MAX_uint16)
		{
			return DefaultPort;
		}

		return static_cast<uint16>(RequestedPort);
	}

	void AppendToolJsonArray(const TArray<FSmithUEToolSchema>& Tools, TArray<TSharedPtr<FJsonValue>>& OutJsonTools)
	{
		OutJsonTools.Reserve(Tools.Num());
		for (const FSmithUEToolSchema& Tool : Tools)
		{
			OutJsonTools.Add(MakeShared<FJsonValueObject>(Tool.ToJsonSchema()));
		}
	}

	bool IsWorkerSafeCommand(const FString& CommandName)
	{
		return CommandName.Equals(TEXT("ping"), ESearchCase::IgnoreCase) ||
			CommandName.Equals(TEXT("list_tools"), ESearchCase::IgnoreCase) ||
			CommandName.Equals(TEXT("get_protocol_info"), ESearchCase::IgnoreCase) ||
			CommandName.Equals(TEXT("get_active_dialog"), ESearchCase::IgnoreCase) ||
			CommandName.Equals(TEXT("dismiss_active_dialog"), ESearchCase::IgnoreCase) ||
			CommandName.Equals(TEXT("set_dialog_auto_response"), ESearchCase::IgnoreCase) ||
			CommandName.Equals(TEXT("get_job_status"), ESearchCase::IgnoreCase);
	}

	TSharedPtr<FJsonObject> DispatchWorkerSafeCommand(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
	{
		if (CommandName.Equals(TEXT("ping"), ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("status"), TEXT("success"));
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("message"), TEXT("pong"));
			Response->SetObjectField(TEXT("data"), Data);
			return Response;
		}

		if (CommandName.Equals(TEXT("list_tools"), ESearchCase::IgnoreCase))
		{
			FString Category;
			if (Params.IsValid())
			{
				Params->TryGetStringField(TEXT("category"), Category);
			}

			const TArray<FSmithUEToolSchema> Tools = Category.IsEmpty()
				? FSmithUEToolRegistry::Get().GetAll()
				: FSmithUEToolRegistry::Get().GetByCategory(Category);

			TArray<TSharedPtr<FJsonValue>> JsonTools;
			AppendToolJsonArray(Tools, JsonTools);

			TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("status"), TEXT("success"));
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("protocol_version"), TEXT("1.0"));
			Data->SetArrayField(TEXT("tools"), JsonTools);
			Response->SetObjectField(TEXT("data"), Data);
			return Response;
		}

		if (CommandName.Equals(TEXT("get_protocol_info"), ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("status"), TEXT("success"));

			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("protocol_version"), TEXT("1.0"));
			Data->SetStringField(TEXT("server_name"), TEXT("SmithUE"));
			Data->SetStringField(TEXT("ue_version"), TEXT("5.8"));

			TArray<TSharedPtr<FJsonValue>> SupportedDomains;
			for (const TCHAR* Domain : {TEXT("Editor"), TEXT("Asset"), TEXT("Material"), TEXT("Project"), TEXT("Blueprint")})
			{
				SupportedDomains.Add(MakeShared<FJsonValueString>(FString(Domain)));
			}
			Data->SetArrayField(TEXT("supported_domains"), SupportedDomains);
			Data->SetNumberField(TEXT("tcp_port"), GetPortFromCommandLine(TEXT("SmithUEport="), 13720));
			Data->SetNumberField(TEXT("http_port"), GetPortFromCommandLine(TEXT("SmithUEhttpport="), 13721));
			Data->SetStringField(TEXT("framing_type"), TEXT("length_prefix_le32"));

			Response->SetObjectField(TEXT("data"), Data);
			return Response;
		}

		if (CommandName.Equals(TEXT("get_active_dialog"), ESearchCase::IgnoreCase))
		{
			return FSmithUEDialogCommands::HandleGetActiveDialog(Params);
		}

		if (CommandName.Equals(TEXT("dismiss_active_dialog"), ESearchCase::IgnoreCase))
		{
			return FSmithUEDialogCommands::HandleDismissActiveDialog(Params);
		}

		if (CommandName.Equals(TEXT("set_dialog_auto_response"), ESearchCase::IgnoreCase))
		{
			return FSmithUEDialogCommands::HandleSetDialogAutoResponse(Params);
		}

		if (CommandName.Equals(TEXT("get_job_status"), ESearchCase::IgnoreCase))
		{
			// Worker-safe: read the progress beacon while the game thread is busy
			// inside a long batch command. Pure atomic reads, no GEditor/UObject.
			FSmithUEProgress& P = FSmithUEProgress::Get();
			const int32 Processed = P.GetProcessed();
			const int32 Total = P.GetTotal();
			const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
			TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("status"), TEXT("success"));
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("active"), P.IsActive());
			Data->SetNumberField(TEXT("job_id"), (double)P.GetJobId());
			Data->SetStringField(TEXT("operation"), P.GetOperation());
			Data->SetNumberField(TEXT("processed"), Processed);
			Data->SetNumberField(TEXT("total"), Total);
			Data->SetNumberField(TEXT("percent"), Total > 0 ? FMath::RoundToInt(100.0 * Processed / Total) : 0);
			Data->SetStringField(TEXT("current_item"), P.GetCurrentItem());
			Data->SetNumberField(TEXT("elapsed_seconds"), (double)(Now - P.GetStartUnixSeconds()));
			Data->SetNumberField(TEXT("seconds_since_update"), (double)(Now - P.GetLastUpdateUnixSeconds()));
			Response->SetObjectField(TEXT("data"), Data);
			return Response;
		}

		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown command: %s"), *CommandName));
	}

	FString DispatchWorkerSafeCommandAsJson(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
	{
		return FSmithUECommonUtils::SerializeJson(DispatchWorkerSafeCommand(CommandName, Params));
	}

	bool IsNidString(const FString& Value)
	{
		return Value.StartsWith(TEXT("N")) && Value.Mid(1).IsNumeric();
	}

	bool JsonValueContainsNid(const TSharedPtr<FJsonValue>& Value);

	bool JsonObjectContainsNid(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		for (const auto& Pair : Object->Values)
		{
			if (JsonValueContainsNid(Pair.Value))
			{
				return true;
			}
		}

		return false;
	}

	bool JsonValueContainsNid(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		switch (Value->Type)
		{
		case EJson::String:
			return IsNidString(Value->AsString());
		case EJson::Array:
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				if (JsonValueContainsNid(Item))
				{
					return true;
				}
			}
			return false;
		case EJson::Object:
			return JsonObjectContainsNid(Value->AsObject());
		default:
			return false;
		}
	}

	// Runs Fn on the game thread and blocks the calling (worker) thread for its result.
	// Uses the CoreTicker (not AsyncTask) so Fn executes directly within the engine's frame
	// tick call stack, where the game thread's inherited FAppTime time-context is live. This
	// matters for tools that trigger rendering (thumbnail render, material compile, viewport
	// draw): render/streaming child tasks then inherit a valid time context instead of hitting
	// the non-fatal "IsInGameThread() FAppTime" ensure. AddTicker is thread-safe (FTSTicker);
	// the delegate returns false to run one-shot.
	template <typename ResultType, typename FnType>
	ResultType RunOnGameThreadWithTimeContext(FnType Fn)
	{
		TSharedRef<TPromise<ResultType>> Promise = MakeShared<TPromise<ResultType>>();
		TFuture<ResultType> Future = Promise->GetFuture();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Fn = MoveTemp(Fn), Promise](float) -> bool
			{
				Promise->SetValue(Fn());
				return false; // one-shot
			}));
		return Future.Get();
	}

	FString DispatchNidCommandSyncOnGameThread(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
	{
		check(!IsInGameThread());
		return RunOnGameThreadWithTimeContext<FString>([CommandName, Params]() -> FString
		{
			check(IsInGameThread());
			TSharedPtr<FJsonObject> Result = FSmithUEToolRegistry::Get().DispatchCommand(CommandName, Params);
			return FSmithUECommonUtils::SerializeJson(Result);
		});
	}

	FString ExtractTaskIdFromPath(const FString& Path)
	{
		static const FString Prefix = TEXT("/api/v1/async/");
		if (!Path.StartsWith(Prefix))
		{
			return FString();
		}
		return Path.RightChop(Prefix.Len());
	}

	bool SendAll(FSocket& ClientSocket, const FString& Response)
	{
		FTCHARToUTF8 Utf8Response(*Response);
		int32 TotalSent = 0;
		while (TotalSent < Utf8Response.Length())
		{
			int32 BytesSent = 0;
			if (!ClientSocket.Send(reinterpret_cast<const uint8*>(Utf8Response.Get()) + TotalSent, Utf8Response.Length() - TotalSent, BytesSent))
			{
				return false;
			}
			if (BytesSent <= 0)
			{
				return false;
			}
			TotalSent += BytesSent;
		}
		return true;
	}

	struct FRouteResult
	{
		int32 StatusCode = 200;
		FString StatusText = TEXT("OK");
		FString Body;
	};

	FRouteResult RouteRequest(const FParsedHttpRequest& Request, USmithUEHttpServer* InServer)
	{
		FRouteResult Result;

		if (Request.Method.Equals(TEXT("OPTIONS"), ESearchCase::IgnoreCase))
		{
			Result.StatusCode = 204;
			Result.StatusText = TEXT("No Content");
			return Result;
		}

		const FString* ExpectedPid = Request.Headers.Find(TEXT("x-smithue-expected-pid"));
		const FString* ExpectedInstance = Request.Headers.Find(TEXT("x-smithue-expected-instance"));
		if ((ExpectedPid && *ExpectedPid != FString::Printf(TEXT("%u"), FPlatformProcess::GetCurrentProcessId()))
			|| (ExpectedInstance && (!InServer || *ExpectedInstance != InServer->InstanceId)))
		{
			Result.StatusCode = 409;
			Result.StatusText = TEXT("Conflict");
			Result.Body = MakeErrorBody(TEXT("Editor instance identity mismatch. Request was not executed."), TEXT("INSTANCE_MISMATCH"));
			return Result;
		}

		// ------------------------------------------------------------------
		//  GET /ready  — startup guard probe; non-blocking atomic read only
		// ------------------------------------------------------------------
		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path == TEXT("/ready"))
		{
			const bool bReady = InServer ? static_cast<bool>(InServer->bIsReady) : false;
			if (!bReady)
			{
				Result.StatusCode = 503;
				Result.StatusText = TEXT("Service Unavailable");
				Result.Body = FString::Printf(TEXT("{\"ready\":false,\"pid\":%u,\"instance_id\":\"%s\"}"),
					FPlatformProcess::GetCurrentProcessId(), InServer ? *InServer->InstanceId : TEXT(""));
				return Result;
			}

			const FString Version = InServer ? InServer->PluginVersion : TEXT("unknown");
			const bool bPIE = InServer ? static_cast<bool>(InServer->bPIEActive) : false;
			const bool bModal = FSmithUEDialogWatcher::Get().IsModalActive();
			Result.Body = FString::Printf(
				TEXT("{\"ready\":true,\"pid\":%u,\"instance_id\":\"%s\",\"version\":\"%s\",\"pie_active\":%s,\"modal_active\":%s}"),
				FPlatformProcess::GetCurrentProcessId(), InServer ? *InServer->InstanceId : TEXT(""), *Version,
				bPIE ? TEXT("true") : TEXT("false"),
				bModal ? TEXT("true") : TEXT("false"));
			return Result;
		}

		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path == TEXT("/api/v1/health"))
		{
			Result.Body = DispatchWorkerSafeCommandAsJson(TEXT("ping"), MakeShared<FJsonObject>());
			return Result;
		}

		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path == TEXT("/api/v1/tools"))
		{
			Result.Body = DispatchWorkerSafeCommandAsJson(TEXT("list_tools"), MakeShared<FJsonObject>());
			return Result;
		}

		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path.StartsWith(TEXT("/api/v1/async/")))
		{
			const FString TaskId = ExtractTaskIdFromPath(Request.Path);
			if (TaskId.IsEmpty())
			{
				Result.StatusCode = 404;
				Result.StatusText = TEXT("Not Found");
				Result.Body = MakeErrorBody(TEXT("Route not found"));
				return Result;
			}

			Result.Body = FSmithUEDispatcher::Get().GetAsyncResult(TaskId);
			return Result;
		}

		if (Request.Method.Equals(TEXT("POST"), ESearchCase::IgnoreCase) && (Request.Path == TEXT("/api/v1/execute") || Request.Path == TEXT("/api/v1/async")))
		{
			const TSharedPtr<FJsonObject> RequestJson = FSmithUECommonUtils::ParseJson(Request.Body);
			if (!RequestJson.IsValid())
			{
				Result.StatusCode = 400;
				Result.StatusText = TEXT("Bad Request");
				Result.Body = MakeErrorBody(TEXT("Invalid JSON body"));
				return Result;
			}

			FString CommandName;
			if (!RequestJson->TryGetStringField(TEXT("command"), CommandName) || CommandName.IsEmpty())
			{
				Result.StatusCode = 400;
				Result.StatusText = TEXT("Bad Request");
				Result.Body = MakeErrorBody(TEXT("Missing 'command' field"));
				return Result;
			}

			// ------------------------------------------------------------------
			//  Startup guard: block non-ping commands until editor is ready
			// ------------------------------------------------------------------
			if (Request.Path == TEXT("/api/v1/execute"))
			{
				const bool bReady = InServer ? static_cast<bool>(InServer->bIsReady) : false;
				if (!bReady && !CommandName.Equals(TEXT("ping"), ESearchCase::IgnoreCase))
				{
					Result.StatusCode = 503;
					Result.StatusText = TEXT("Service Unavailable");
					Result.Body       = MakeErrorBody(TEXT("Editor not ready"), TEXT("EDITOR_NOT_READY"));
					return Result;
				}
			}

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* ParamsField = nullptr;
			if (RequestJson->TryGetObjectField(TEXT("params"), ParamsField) && ParamsField != nullptr && ParamsField->IsValid())
			{
				Params = *ParamsField;
			}

			if (Request.Path == TEXT("/api/v1/async"))
			{
				Result.Body = FSmithUEDispatcher::Get().DispatchAsync(CommandName, Params);
			}
			else if (IsWorkerSafeCommand(CommandName))
			{
				Result.Body = DispatchWorkerSafeCommandAsJson(CommandName, Params);
			}
			else if (JsonObjectContainsNid(Params) && !IsInGameThread())
			{
				Result.Body = DispatchNidCommandSyncOnGameThread(CommandName, Params);
			}
			else
			{
				Result.Body = FSmithUEDispatcher::Get().DispatchSync(CommandName, Params);
			}
			return Result;
		}

		Result.StatusCode = 404;
		Result.StatusText = TEXT("Not Found");
		Result.Body = MakeErrorBody(TEXT("Route not found"));
		return Result;
	}
}

bool FSmithUEHttpServerRunnable::IsGameThreadRequired(const FParsedHttpRequest& Request)
{
	// GET /ready — lightweight atomic read, no UObject access
	if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path == TEXT("/ready"))
	{
		return false;
	}

	// GET health/tools map to audited worker-safe system commands.
	if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) &&
		(Request.Path == TEXT("/api/v1/health") || Request.Path == TEXT("/api/v1/tools")))
	{
		return false;
	}

	// POST /api/v1/execute — check command name
	if (Request.Method.Equals(TEXT("POST"), ESearchCase::IgnoreCase) &&
		Request.Path == TEXT("/api/v1/execute"))
	{
		// Parse command name from JSON body to identify lightweight commands.
		// Parse safely: if JSON parsing fails, default to game thread (safe).
		const TSharedPtr<FJsonObject> Body = FSmithUECommonUtils::ParseJson(Request.Body);
		if (Body.IsValid())
		{
			FString Command;
			if (Body->TryGetStringField(TEXT("command"), Command))
			{
				// These commands don't access UObjects and can run on worker threads.
				if (SmithUEHttpServer::Private::IsWorkerSafeCommand(Command))
				{
					return false;
				}
			}
		}
	}

	// Default: require game thread (safe)
	return true;
}

FSmithUEHttpServerRunnable::FSmithUEHttpServerRunnable(USmithUEHttpServer* InServer, TSharedPtr<FSocket> InListenerSocket)
	: Server(InServer)
	, ListenerSocket(InListenerSocket)
	, bStopping(false)
{
}

FSmithUEHttpServerRunnable::~FSmithUEHttpServerRunnable()
{
}

bool FSmithUEHttpServerRunnable::Init()
{
	return Server != nullptr && ListenerSocket.IsValid();
}

uint32 FSmithUEHttpServerRunnable::Run()
{
	while (!bStopping)
	{
		bool bPendingConnection = false;
		if (!ListenerSocket.IsValid() || !ListenerSocket->HasPendingConnection(bPendingConnection) || !bPendingConnection)
		{
			FPlatformProcess::Sleep(0.05f);
			continue;
		}

		FSocket* RawClientSocket = ListenerSocket->Accept(TEXT("SmithUEHttpClient"));
		TSharedPtr<FSocket, ESPMode::ThreadSafe> ClientSocket;
		if (RawClientSocket != nullptr)
		{
			ClientSocket = TSharedPtr<FSocket, ESPMode::ThreadSafe>(RawClientSocket, ::FSocketDeleter(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)));
		}

		if (!ClientSocket.IsValid())
		{
			FPlatformProcess::Sleep(0.05f);
			continue;
		}

		ClientSocket->SetNoDelay(true);
		ClientSocket->SetNonBlocking(true);

		if (bStopping)
		{
			ClientSocket->Close();
			continue;
		}

		if (ActiveWorkerCount.GetValue() >= MaxConcurrentWorkers)
		{
			const FString BusyResponse = SmithUEHttpServer::Private::BuildHttpResponse(503, TEXT("Service Unavailable"),
				FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateErrorResponse(TEXT("Server busy: too many concurrent requests"), TEXT("INTERNAL_ERROR"))));
			SmithUEHttpServer::Private::SendAll(*ClientSocket, BusyResponse);
			ClientSocket->Close();
			continue;
		}

		ActiveWorkerCount.Increment();
		USmithUEHttpServer* ServerPtr = Server;
		Async(EAsyncExecution::ThreadPool, [this, ClientSocket, ServerPtr]()
		{
			struct FActiveWorkerScope
			{
				FThreadSafeCounter& Counter;
				~FActiveWorkerScope()
				{
					Counter.Decrement();
				}
			} ActiveWorkerScope{ActiveWorkerCount};

			SmithUEHttpServer::Private::FParsedHttpRequest Request;
			FString Response;
			const SmithUEHttpServer::Private::EHttpRequestReceiveResult ReceiveResult = SmithUEHttpServer::Private::ReceiveHttpRequest(*ClientSocket, bStopping, Request);
			if (ReceiveResult == SmithUEHttpServer::Private::EHttpRequestReceiveResult::Success)
			{
				SmithUEHttpServer::Private::FRouteResult RouteResult;
				if (IsGameThreadRequired(Request))
				{
					// Gate game-thread requests separately so worker-safe commands
					// (ping, Dialog domain) keep responding while the game thread is
					// jammed (e.g. blocked behind a modal dialog or a long asset op).
					if (ActiveGameThreadCount.GetValue() >= MaxGameThreadWorkers)
					{
						Response = SmithUEHttpServer::Private::BuildHttpResponse(503, TEXT("Service Unavailable"),
							FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateErrorResponse(
								TEXT("Server busy: too many pending game-thread commands (worker-safe commands like get_active_dialog still work)"), TEXT("INTERNAL_ERROR"))));
						if (!SmithUEHttpServer::Private::SendAll(*ClientSocket, Response))
						{
							UE_LOG(LogSmithUE, Verbose, TEXT("Failed to send HTTP busy response to SmithUE client"));
						}
						ClientSocket->Close();
						return;
					}

					ActiveGameThreadCount.Increment();
					struct FGameThreadScope
					{
						FThreadSafeCounter& Counter;
						~FGameThreadScope() { Counter.Decrement(); }
					} GameThreadScope{ActiveGameThreadCount};

					RouteResult = SmithUEHttpServer::Private::RunOnGameThreadWithTimeContext<SmithUEHttpServer::Private::FRouteResult>(
						[Request, ServerPtr]() -> SmithUEHttpServer::Private::FRouteResult
						{
							check(IsInGameThread());
							return SmithUEHttpServer::Private::RouteRequest(Request, ServerPtr);
						});
				}
				else
				{
					RouteResult = SmithUEHttpServer::Private::RouteRequest(Request, ServerPtr);
				}
				Response = RouteResult.StatusCode == 204
					? SmithUEHttpServer::Private::BuildNoContentResponse()
					: SmithUEHttpServer::Private::BuildHttpResponse(RouteResult.StatusCode, RouteResult.StatusText, RouteResult.Body);
			}
			else if (ReceiveResult == SmithUEHttpServer::Private::EHttpRequestReceiveResult::PayloadTooLarge)
			{
				Response = SmithUEHttpServer::Private::BuildHttpResponse(413, TEXT("Payload Too Large"),
					FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateErrorResponse(TEXT("payload too large"), TEXT("PAYLOAD_TOO_LARGE"))));
			}
			else
			{
				Response = SmithUEHttpServer::Private::BuildHttpResponse(400, TEXT("Bad Request"), SmithUEHttpServer::Private::MakeErrorBody(TEXT("Failed to parse HTTP request")));
			}

			if (!SmithUEHttpServer::Private::SendAll(*ClientSocket, Response))
			{
				UE_LOG(LogSmithUE, Verbose, TEXT("Failed to send HTTP response to SmithUE client"));
			}

			ClientSocket->Close();
		});
	}

	return 0;
}

void FSmithUEHttpServerRunnable::Stop()
{
	bStopping = true;
}

void FSmithUEHttpServerRunnable::Exit()
{
}
