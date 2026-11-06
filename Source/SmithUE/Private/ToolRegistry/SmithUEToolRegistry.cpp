// Copyright 2026, 123dx-svg. MIT License.

#include "ToolRegistry/SmithUEToolRegistry.h"

#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/SmithUECommonUtils.h"

namespace
{
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

}

FSmithUEToolRegistry& FSmithUEToolRegistry::Get()
{
	static FSmithUEToolRegistry Instance;
	return Instance;
}

void FSmithUEToolRegistry::Register(const FSmithUEToolSchema& Schema, FSmithUECommandHandler Handler)
{
	Tools.Add(Schema.Name, Schema);
	Handlers.Add(Schema.Name, MoveTemp(Handler));
}

const FSmithUEToolSchema* FSmithUEToolRegistry::Find(const FString& Name) const
{
	return Tools.Find(Name);
}

TSharedPtr<FJsonObject> FSmithUEToolRegistry::DispatchCommand(const FString& Name, const TSharedPtr<FJsonObject>& Params)
{
	check(IsInGameThread());

	// --- Editor state pre-check ---
	// Readonly commands bypass the busy check.
	static const TArray<FString> ReadonlyCommands = {
		TEXT("ping"),
		TEXT("bp_describe_graph"),
		TEXT("bp_get_summary"),
		TEXT("livecoding_status"),
	};
	const bool bIsReadonly = ReadonlyCommands.Contains(Name) || Name.StartsWith(TEXT("observation_")) || Name.StartsWith(TEXT("pie_"));

	if (!bIsReadonly)
	{
		if (GEditor && GEditor->PlayWorld != nullptr)
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				TEXT("Editor is busy: PIE running. Command rejected. Please stop PIE first."),
				TEXT("PIE_LOCKED"));
		}

		// livecoding_status is a registered read-only tool (safe during PIE).
		// livecoding_compile is intentionally PIE-blocked (hot-compiling C++ during PIE is unsafe).
	}
	// --- End editor state pre-check ---

	// --- Metrics: measure request bytes ---
	int64 RequestBytes = 0;
	if (Params.IsValid())
	{
		FString ParamsStr;
		TSharedRef<TJsonWriter<>> ParamsWriter = TJsonWriterFactory<>::Create(&ParamsStr);
		FJsonSerializer::Serialize(Params.ToSharedRef(), ParamsWriter);
		RequestBytes = ParamsStr.Len();
	}

	const double StartTime = FPlatformTime::Seconds();

	TSharedPtr<FJsonObject> Response;
	if (const FSmithUECommandHandler* Handler = Handlers.Find(Name))
	{
		Response = (*Handler)(Params);
	}
	else
	{
		Response = FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown command: %s"), *Name));
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	// --- Metrics: measure response bytes ---
	int64 ResponseBytes = 0;
	if (Response.IsValid())
	{
		FString ResponseStr;
		TSharedRef<TJsonWriter<>> ResponseWriter = TJsonWriterFactory<>::Create(&ResponseStr);
		FJsonSerializer::Serialize(Response.ToSharedRef(), ResponseWriter);
		ResponseBytes = ResponseStr.Len();
	}

	// --- Metrics: update (system_ commands skip CallCount) ---
	const bool bIsSystemCommand = Name.StartsWith(TEXT("system_"));
	if (!bIsSystemCommand)
	{
		// Retry detection: same command name as last, and last returned error
		if (Name == Metrics.LastCommandName && Metrics.bLastCommandWasError)
		{
			Metrics.RetryCount++;
		}

		Metrics.CallCount++;
		Metrics.TotalRequestBytes += RequestBytes;
		Metrics.TotalResponseBytes += ResponseBytes;
		Metrics.TotalExecutionMs += ElapsedMs;

		FSmithUEPerCommandMetrics& PerCmd = Metrics.PerCommand.FindOrAdd(Name);
		PerCmd.Count++;
		PerCmd.TotalResponseBytes += ResponseBytes;

		const bool bWasError = Response.IsValid() && Response->HasField(TEXT("error"));
		Metrics.bLastCommandWasError = bWasError;
		Metrics.LastCommandName = Name;
		Metrics.LastRequestBytes = RequestBytes;
		Metrics.LastResponseBytes = ResponseBytes;
		Metrics.LastExecutionMs = ElapsedMs;
	}

	return Response;
}

TArray<FSmithUEToolSchema> FSmithUEToolRegistry::GetAll() const
{
	TArray<FSmithUEToolSchema> Result;
	Result.Reserve(Tools.Num());
	for (const TPair<FString, FSmithUEToolSchema>& Pair : Tools)
	{
		Result.Add(Pair.Value);
	}
	return Result;
}

TArray<FSmithUEToolSchema> FSmithUEToolRegistry::GetByCategory(const FString& Category) const
{
	TArray<FSmithUEToolSchema> Result;
	for (const TPair<FString, FSmithUEToolSchema>& Pair : Tools)
	{
		if (Pair.Value.Category == Category)
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

FString FSmithUEToolRegistry::ExportAllAsJsonSchema() const
{
	TArray<TSharedPtr<FJsonValue>> JsonTools;
	JsonTools.Reserve(Tools.Num());
	for (const TPair<FString, FSmithUEToolSchema>& Pair : Tools)
	{
		JsonTools.Add(MakeShared<FJsonValueObject>(Pair.Value.ToJsonSchema()));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("tools"), JsonTools);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

void FSmithUEToolRegistry::Reset()
{
	check(IsInGameThread());

	Tools.Empty();
	Handlers.Empty();
	NidSession.NidMap.Empty();
	NidSession.StaleFlags.Empty();
}

void FSmithUEToolRegistry::RegisterBuiltinCommands()
{
	FSmithUEToolRegistry& Registry = Get();

	if (Registry.Find(TEXT("ping")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(TEXT("ping"), TEXT("System"), TEXT("Test server connectivity")),
			[](const TSharedPtr<FJsonObject>&) -> TSharedPtr<FJsonObject>
			{
				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));
				TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
				Data->SetStringField(TEXT("message"), TEXT("pong"));
				Response->SetObjectField(TEXT("data"), Data);
				return Response;
			});
	}

	if (Registry.Find(TEXT("list_tools")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(
				TEXT("list_tools"),
				TEXT("System"),
				TEXT("List all available commands with schemas"),
				{
					FSmithUEToolParam(TEXT("category"), TEXT("string"), TEXT("Optional category filter"), false)
				}),
			[](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
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
			});
	}

	if (Registry.Find(TEXT("get_protocol_info")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(TEXT("get_protocol_info"), TEXT("System"), TEXT("Get protocol and transport information")),
			[](const TSharedPtr<FJsonObject>&) -> TSharedPtr<FJsonObject>
			{
				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));

				TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
				Data->SetStringField(TEXT("protocol_version"), TEXT("1.0"));
				Data->SetStringField(TEXT("server_name"), TEXT("SmithUE"));
				Data->SetStringField(TEXT("ue_version"), TEXT("5.2"));

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
			});
	}

	// --- Metrics commands ---
	if (Registry.Find(TEXT("system_get_metrics")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(TEXT("system_get_metrics"), TEXT("System"), TEXT("Return current session command metrics")),
			[](const TSharedPtr<FJsonObject>&) -> TSharedPtr<FJsonObject>
			{
				const FSmithUECommandMetrics& M = FSmithUEToolRegistry::Get().Metrics;

				TSharedPtr<FJsonObject> MetricsObj = MakeShared<FJsonObject>();
				MetricsObj->SetNumberField(TEXT("call_count"), M.CallCount);
				MetricsObj->SetNumberField(TEXT("total_request_bytes"), static_cast<double>(M.TotalRequestBytes));
				MetricsObj->SetNumberField(TEXT("total_response_bytes"), static_cast<double>(M.TotalResponseBytes));
				MetricsObj->SetNumberField(TEXT("retry_count"), M.RetryCount);
				const double AvgMs = M.CallCount > 0 ? M.TotalExecutionMs / M.CallCount : 0.0;
				MetricsObj->SetNumberField(TEXT("avg_execution_ms"), AvgMs);

				TSharedPtr<FJsonObject> PerCommandObj = MakeShared<FJsonObject>();
				for (const TPair<FString, FSmithUEPerCommandMetrics>& Pair : M.PerCommand)
				{
					TSharedPtr<FJsonObject> CmdObj = MakeShared<FJsonObject>();
					CmdObj->SetNumberField(TEXT("count"), Pair.Value.Count);
					CmdObj->SetNumberField(TEXT("total_response_bytes"), static_cast<double>(Pair.Value.TotalResponseBytes));
					PerCommandObj->SetObjectField(Pair.Key, CmdObj);
				}
				MetricsObj->SetObjectField(TEXT("per_command"), PerCommandObj);

				TSharedPtr<FJsonObject> LastCmd = MakeShared<FJsonObject>();
				LastCmd->SetStringField(TEXT("name"), M.LastCommandName);
				LastCmd->SetNumberField(TEXT("request_bytes"), static_cast<double>(M.LastRequestBytes));
				LastCmd->SetNumberField(TEXT("response_bytes"), static_cast<double>(M.LastResponseBytes));
				LastCmd->SetNumberField(TEXT("execution_ms"), M.LastExecutionMs);
				MetricsObj->SetObjectField(TEXT("last_command"), LastCmd);

				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));
				Response->SetObjectField(TEXT("metrics"), MetricsObj);
				return Response;
			});
	}

	if (Registry.Find(TEXT("system_reset_metrics")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(TEXT("system_reset_metrics"), TEXT("System"), TEXT("Reset all command metrics counters")),
			[](const TSharedPtr<FJsonObject>&) -> TSharedPtr<FJsonObject>
			{
				FSmithUEToolRegistry::Get().Metrics = FSmithUECommandMetrics{};

				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));
				return Response;
			});
	}
}
