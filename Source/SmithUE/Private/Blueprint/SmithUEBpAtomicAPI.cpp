// Copyright 2026, 123dx-svg. MIT License.

#include "Blueprint/SmithUEBpAtomicAPI.h"

#include "SmithUEModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "Blueprint/SmithUEBpAtomicAPIHelpers.h"
#include "Components/ActorComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimNodeBase.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimationGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "PhysicsEngine/BodySetup.h"
#include "GameFramework/MovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Editor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Engine/TimelineTemplate.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputKey.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "InputAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintCompilationManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/OutputDeviceNull.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Utils/SmithUECommonUtils.h"

using namespace SmithUEBpAtomicAPIHelpers;

namespace
{
	// --- Collision helpers (bp_set_component_collision) -----------------------

	// Resolve a collision channel by its editor display name. Handles project
	// renames in DefaultEngine.ini, e.g. "Vehicle" -> ECC_Vehicle, "Pawn" -> ECC_Pawn.
	bool ResolveCollisionChannelByName(const FString& DisplayName, ECollisionChannel& OutChannel)
	{
		UCollisionProfile* Profile = UCollisionProfile::Get();
		if (!Profile)
		{
			return false;
		}
		const FName Target(*DisplayName);
		for (int32 Index = 0; Index < ECC_MAX; ++Index)
		{
			if (Profile->ReturnChannelNameFromContainerIndex(Index) == Target)
			{
				OutChannel = static_cast<ECollisionChannel>(Index);
				return true;
			}
		}
		return false;
	}

	FString CollisionChannelDisplayName(ECollisionChannel Channel)
	{
		if (UCollisionProfile* Profile = UCollisionProfile::Get())
		{
			const FName Name = Profile->ReturnChannelNameFromContainerIndex(static_cast<int32>(Channel));
			if (Name != NAME_None)
			{
				return Name.ToString();
			}
		}
		return FString::Printf(TEXT("Channel_%d"), static_cast<int32>(Channel));
	}

	bool ParseCollisionResponse(const FString& In, ECollisionResponse& Out)
	{
		if (In.Equals(TEXT("Ignore"), ESearchCase::IgnoreCase)) { Out = ECR_Ignore; return true; }
		if (In.Equals(TEXT("Overlap"), ESearchCase::IgnoreCase)) { Out = ECR_Overlap; return true; }
		if (In.Equals(TEXT("Block"), ESearchCase::IgnoreCase)) { Out = ECR_Block; return true; }
		return false;
	}

	const TCHAR* CollisionResponseName(ECollisionResponse Response)
	{
		switch (Response)
		{
			case ECR_Ignore:  return TEXT("Ignore");
			case ECR_Overlap: return TEXT("Overlap");
			case ECR_Block:   return TEXT("Block");
			default:          return TEXT("Unknown");
		}
	}

	// True if the component's StaticMesh asset has simple collision geometry.
	bool StaticMeshHasCollisionGeometry(UStaticMeshComponent* SMC)
	{
		if (!SMC)
		{
			return false;
		}
		UStaticMesh* Mesh = SMC->GetStaticMesh();
		if (!Mesh)
		{
			return false;
		}
		UBodySetup* BodySetup = Mesh->GetBodySetup();
		if (!BodySetup)
		{
			return false;
		}
		// Match the engine's own definition (UStaticMeshComponent): a mesh "has collision"
		// if it has simple primitives OR uses its render geometry as collision.
		return BodySetup->AggGeom.GetElementCount() > 0
			|| BodySetup->GetCollisionTraceFlag() == CTF_UseComplexAsSimple;
	}

	struct FCollisionApplyConfig
	{
		ECollisionChannel ObjectType = ECC_WorldStatic;
		bool bSetObjectType = false;
		TArray<TPair<ECollisionChannel, ECollisionResponse>> Responses;
		FString ComponentFilter;          // empty = all StaticMeshComponents
		bool bSkipIfNoMeshCollision = true;
		bool bDryRun = false;
	};

	// Apply collision settings to all matching StaticMeshComponent templates in one Blueprint.
	TSharedPtr<FJsonObject> ProcessBlueprintCollision(const FString& BpPath, const FCollisionApplyConfig& Cfg, int32& OutChangedTotal)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("bp_path"), BpPath);
		TArray<TSharedPtr<FJsonValue>> CompResults;

		UBlueprint* Blueprint = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			Result->SetStringField(TEXT("status"), TEXT("error"));
			Result->SetStringField(TEXT("error"), TEXT("Invalid blueprint or no SimpleConstructionScript"));
			Result->SetArrayField(TEXT("components"), CompResults);
			return Result;
		}

		bool bAnyChanged = false;
		int32 LocalChanged = 0;
		int32 LocalSkipped = 0;

		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate)
			{
				continue;
			}
			UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Node->ComponentTemplate);
			if (!SMC)
			{
				continue;
			}
			const FString CompName = Node->GetVariableName().ToString();
			if (!Cfg.ComponentFilter.IsEmpty() && !CompName.Equals(Cfg.ComponentFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}

			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("name"), CompName);

			if (Cfg.bSkipIfNoMeshCollision && !StaticMeshHasCollisionGeometry(SMC))
			{
				C->SetStringField(TEXT("action"), TEXT("skipped"));
				C->SetStringField(TEXT("reason"), SMC->GetStaticMesh() ? TEXT("static mesh has no collision geometry") : TEXT("no static mesh assigned"));
				++LocalSkipped;
				CompResults.Add(MakeShared<FJsonValueObject>(C));
				continue;
			}

			C->SetStringField(TEXT("before_profile"), SMC->GetCollisionProfileName().ToString());
			C->SetStringField(TEXT("before_object_type"), CollisionChannelDisplayName(SMC->GetCollisionObjectType()));

			if (!Cfg.bDryRun)
			{
				Blueprint->Modify();
				SMC->Modify();
				// Switch to a Custom profile so per-channel/object-type overrides are not reset by a preset.
				SMC->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
				if (Cfg.bSetObjectType)
				{
					SMC->SetCollisionObjectType(Cfg.ObjectType);
				}
				for (const TPair<ECollisionChannel, ECollisionResponse>& Resp : Cfg.Responses)
				{
					SMC->SetCollisionResponseToChannel(Resp.Key, Resp.Value);
				}
				bAnyChanged = true;
			}
			++LocalChanged;

			C->SetStringField(TEXT("action"), Cfg.bDryRun ? TEXT("would_change") : TEXT("changed"));
			C->SetStringField(TEXT("after_object_type"), CollisionChannelDisplayName(SMC->GetCollisionObjectType()));
			TArray<TSharedPtr<FJsonValue>> RespArr;
			for (const TPair<ECollisionChannel, ECollisionResponse>& Resp : Cfg.Responses)
			{
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetStringField(TEXT("channel"), CollisionChannelDisplayName(Resp.Key));
				R->SetStringField(TEXT("response"), CollisionResponseName(Cfg.bDryRun ? Resp.Value : SMC->GetCollisionResponseToChannel(Resp.Key)));
				RespArr.Add(MakeShared<FJsonValueObject>(R));
			}
			C->SetArrayField(TEXT("responses"), RespArr);
			CompResults.Add(MakeShared<FJsonValueObject>(C));
		}

		if (bAnyChanged)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			Blueprint->MarkPackageDirty();
			TArray<FString> CompileErrors;
			FSmithUEBpAtomicAPI::CompileBlueprint(Blueprint, CompileErrors, true);
		}

		OutChangedTotal += LocalChanged;
		Result->SetStringField(TEXT("status"), TEXT("success"));
		Result->SetNumberField(TEXT("changed"), LocalChanged);
		Result->SetNumberField(TEXT("skipped"), LocalSkipped);
		Result->SetArrayField(TEXT("components"), CompResults);
		return Result;
	}

	FString JoinPropertyNames(const TArray<FString>& Names)
	{
		return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : TEXT("<none>");
	}

	void AppendJsonNameArray(TSharedPtr<FJsonObject> Data, const TCHAR* FieldName, const TArray<FString>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Name : Names)
		{
			Values.Add(MakeShared<FJsonValueString>(Name));
		}
		Data->SetArrayField(FieldName, Values);
	}

	TSharedPtr<FJsonObject> CreateMissingAnimBlueprintError(const FString& BpPath)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid bp_path '%s': expected an AnimBlueprint asset. State-machine authoring tools are for AnimBlueprint state machines only; use anim_create_blueprint first if needed."), *BpPath));
	}

	bool IsAnimationGraph(UEdGraph* Graph)
	{
		return Graph && Graph->IsA(UAnimationGraph::StaticClass());
	}

	FString GraphPathKey(const FString& BpPath, const FString& GraphName)
	{
		return BpPath + TEXT("::") + GraphName;
	}

	UAnimGraphNode_StateMachineBase* FindStateMachineNodeForGraph(UBlueprint* Blueprint, UAnimationStateMachineGraph* StateMachineGraph)
	{
		if (!Blueprint || !StateMachineGraph)
		{
			return nullptr;
		}

		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
				if (StateMachineNode && StateMachineNode->EditorStateMachineGraph == StateMachineGraph)
				{
					return StateMachineNode;
				}
			}
		}
		return nullptr;
	}

	UAnimationStateMachineGraph* ResolveStateMachineGraph(UBlueprint* Blueprint, const FString& BpPath, const FString& StateMachineRef, FString& OutError)
	{
		if (!Cast<UAnimBlueprint>(Blueprint))
		{
			OutError = FString::Printf(TEXT("Invalid bp_path '%s': expected an AnimBlueprint. State-machine tools support AnimBlueprint state machines only."), *BpPath);
			return nullptr;
		}

		if (UEdGraph* Graph = FSmithUEBpAtomicAPI::FindGraph(Blueprint, StateMachineRef))
		{
			if (UAnimationStateMachineGraph* StateMachineGraph = Cast<UAnimationStateMachineGraph>(Graph))
			{
				return StateMachineGraph;
			}
			OutError = FString::Printf(TEXT("Graph '%s' is '%s', not a UAnimationStateMachineGraph. Pass the state_machine_graph returned by bp_add_state_machine or the state-machine node_id."), *StateMachineRef, *Graph->GetClass()->GetName());
			return nullptr;
		}

		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!IsAnimationGraph(Graph))
			{
				continue;
			}

			FString ResolveError;
			UEdGraphNode* Node = ResolveNodeId(Graph, GraphPathKey(BpPath, Graph->GetName()), StateMachineRef, ResolveError);
			if (!Node)
			{
				continue;
			}

			UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
			if (!StateMachineNode)
			{
				OutError = FString::Printf(TEXT("Node '%s' is '%s', not a UAnimGraphNode_StateMachine. Pass a state-machine node_id or state-machine graph name."), *StateMachineRef, *Node->GetClass()->GetName());
				return nullptr;
			}
			if (!StateMachineNode->EditorStateMachineGraph)
			{
				OutError = FString::Printf(TEXT("State-machine node '%s' has no EditorStateMachineGraph; recreate it with bp_add_state_machine."), *StateMachineRef);
				return nullptr;
			}
			return StateMachineNode->EditorStateMachineGraph;
		}

		OutError = FString::Printf(TEXT("State machine '%s' not found. Pass either the node_id returned by bp_add_state_machine or the state_machine_graph name; use bp_read_state_machine only after creating one."), *StateMachineRef);
		return nullptr;
	}

	UAnimStateNode* FindAnimStateNode(UAnimationStateMachineGraph* StateMachineGraph, const FString& BpPath, const FString& StateRef, FString& OutError)
	{
		if (!StateMachineGraph)
		{
			OutError = TEXT("State machine graph is null");
			return nullptr;
		}

		if (UEdGraphNode* Node = ResolveNodeId(StateMachineGraph, GraphPathKey(BpPath, StateMachineGraph->GetName()), StateRef, OutError))
		{
			UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
			if (!StateNode)
			{
				OutError = FString::Printf(TEXT("Node '%s' in state machine '%s' is '%s', not a UAnimStateNode. Use bp_read_state_machine to list state node ids/names."), *StateRef, *StateMachineGraph->GetName(), *Node->GetClass()->GetName());
				return nullptr;
			}
			return StateNode;
		}

		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
			if (StateNode && StateNode->GetStateName().Equals(StateRef, ESearchCase::IgnoreCase))
			{
				OutError.Reset();
				return StateNode;
			}
		}

		OutError = FString::Printf(TEXT("State '%s' not found in state machine '%s'. Use bp_read_state_machine to list states."), *StateRef, *StateMachineGraph->GetName());
		return nullptr;
	}

	UAnimStateEntryNode* EnsureStateMachineEntry(UAnimationStateMachineGraph* StateMachineGraph)
	{
		if (!StateMachineGraph)
		{
			return nullptr;
		}

		if (StateMachineGraph->EntryNode)
		{
			return StateMachineGraph->EntryNode;
		}

		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			if (UAnimStateEntryNode* EntryNode = Cast<UAnimStateEntryNode>(Node))
			{
				StateMachineGraph->EntryNode = EntryNode;
				return EntryNode;
			}
		}

		const UEdGraphSchema* Schema = StateMachineGraph->GetSchema();
		if (Schema)
		{
			Schema->CreateDefaultNodesForGraph(*StateMachineGraph);
		}
		return StateMachineGraph->EntryNode;
	}

	UEdGraphPin* FirstPinByDirection(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	bool HasRealStates(UAnimationStateMachineGraph* StateMachineGraph)
	{
		if (!StateMachineGraph)
		{
			return false;
		}
		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			if (Cast<UAnimStateNode>(Node))
			{
				return true;
			}
		}
		return false;
	}

	void MarkStateMachineGraphStale(const FString& BpPath, UAnimationStateMachineGraph* StateMachineGraph)
	{
		if (StateMachineGraph)
		{
			FSmithUEToolRegistry::Get().NidSession.MarkStale(GraphPathKey(BpPath, StateMachineGraph->GetName()));
		}
	}

	UAnimGraphNode_Base* ResolveAnimGraphNode(UBlueprint* Blueprint, UEdGraph* Graph, const FString& BpPath, const FString& GraphName, const FString& NodeId, FString& OutError)
	{
		if (!Blueprint)
		{
			OutError = TEXT("Invalid bp_path: expected an AnimBlueprint/Blueprint asset path that contains an AnimGraph");
			return nullptr;
		}
		if (!Graph)
		{
			OutError = FString::Printf(TEXT("Graph '%s' not found. Use bp_describe_graph or anim_read_blueprint to list graphs; AnimGraph graphs are supported via GetAllGraphs."), *GraphName);
			return nullptr;
		}

		FString ResolveError;
		UEdGraphNode* Node = ResolveNodeId(Graph, BpPath + TEXT("::") + GraphName, NodeId, ResolveError);
		if (!Node)
		{
			OutError = ResolveError.IsEmpty() ? FString::Printf(TEXT("Node '%s' not found in graph '%s'. Re-run bp_describe_graph; node ids can go stale after graph mutation."), *NodeId, *GraphName) : ResolveError;
			return nullptr;
		}

		UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
		if (!AnimNode)
		{
			OutError = FString::Printf(TEXT("Node '%s' is '%s', not a UAnimGraphNode_Base. These tools support AnimGraph anim nodes only; use bp_set_pin_default/bp_connect_pins for regular Blueprint/K2 nodes."), *NodeId, *Node->GetClass()->GetName());
			return nullptr;
		}
		return AnimNode;
	}

	FStructProperty* FindAnimNodeStructProperty(UAnimGraphNode_Base* AnimNode)
	{
		if (!AnimNode)
		{
			return nullptr;
		}

		if (FStructProperty* NamedNodeProperty = FindFProperty<FStructProperty>(AnimNode->GetClass(), FName(TEXT("Node"))))
		{
			return NamedNodeProperty;
		}

		for (TFieldIterator<FStructProperty> It(AnimNode->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FStructProperty* StructProperty = *It;
			if (StructProperty && StructProperty->Struct && StructProperty->Struct->IsChildOf(FAnimNode_Base::StaticStruct()))
			{
				return StructProperty;
			}
		}
		return nullptr;
	}

	TArray<FString> GetAnimNodeInnerPropertyNames(FStructProperty* NodeStructProperty)
	{
		TArray<FString> Names;
		if (!NodeStructProperty || !NodeStructProperty->Struct)
		{
			return Names;
		}

		for (TFieldIterator<FProperty> It(NodeStructProperty->Struct); It; ++It)
		{
			if (FProperty* Property = *It)
			{
				Names.Add(Property->GetName());
			}
		}
		Names.Sort();
		return Names;
	}

	FProperty* FindAnimNodeInnerProperty(FStructProperty* NodeStructProperty, const FString& PropertyName)
	{
		if (!NodeStructProperty || !NodeStructProperty->Struct)
		{
			return nullptr;
		}

		const FName DesiredName(*PropertyName);
		if (FProperty* Exact = NodeStructProperty->Struct->FindPropertyByName(DesiredName))
		{
			return Exact;
		}

		for (TFieldIterator<FProperty> It(NodeStructProperty->Struct); It; ++It)
		{
			FProperty* Candidate = *It;
			if (Candidate && Candidate->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	int32 FindOptionalAnimPinIndex(UAnimGraphNode_Base* AnimNode, const FString& PropertyName)
	{
		if (!AnimNode)
		{
			return INDEX_NONE;
		}

		for (int32 Index = 0; Index < AnimNode->ShowPinForProperties.Num(); ++Index)
		{
			if (AnimNode->ShowPinForProperties[Index].PropertyName.ToString().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/**
	 * UE5.8: UAnimGraphNode_Base::PropertyBindings was moved into a separate
	 * instanced UAnimGraphNodeBinding subobject (the concrete type
	 * UAnimGraphNodeBinding_Base lives in a Private engine header, so it cannot
	 * be included by a plugin). We reach the map purely through reflection:
	 *   AnimNode->Binding (UPROPERTY, TObjectPtr<UAnimGraphNodeBinding>)
	 *     -> PropertyBindings (UPROPERTY, TMap<FName, FAnimGraphNodePropertyBinding>)
	 * The map property address is reinterpreted as the concrete TMap (layout is
	 * the real type, so this is safe). Returns nullptr when the binding subobject
	 * is absent (which cannot be created from a plugin without engine-private API).
	 */
	TMap<FName, FAnimGraphNodePropertyBinding>* GetAnimNodePropertyBindings(UAnimGraphNode_Base* AnimNode)
	{
		if (!AnimNode)
		{
			return nullptr;
		}

		FObjectProperty* BindingProp = CastField<FObjectProperty>(AnimNode->GetClass()->FindPropertyByName(TEXT("Binding")));
		if (!BindingProp)
		{
			return nullptr;
		}

		UObject* BindingObj = BindingProp->GetObjectPropertyValue_InContainer(AnimNode);
		if (!BindingObj)
		{
			return nullptr;
		}

		FMapProperty* MapProp = CastField<FMapProperty>(BindingObj->GetClass()->FindPropertyByName(TEXT("PropertyBindings")));
		if (!MapProp)
		{
			return nullptr;
		}

		return reinterpret_cast<TMap<FName, FAnimGraphNodePropertyBinding>*>(MapProp->ContainerPtrToValuePtr<void>(BindingObj));
	}

	TArray<FString> GetOptionalAnimPinNames(UAnimGraphNode_Base* AnimNode)
	{
		TArray<FString> Names;
		if (!AnimNode)
		{
			return Names;
		}

		for (const FOptionalPinFromProperty& OptionalPin : AnimNode->ShowPinForProperties)
		{
			Names.Add(OptionalPin.PropertyName.ToString());
		}
		Names.Sort();
		return Names;
	}

	bool BlueprintHasMemberVariable(UBlueprint* Blueprint, const FString& VariableName)
	{
		if (!Blueprint || VariableName.IsEmpty())
		{
			return false;
		}

		const FName DesiredName(*VariableName);
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (Variable.VarName == DesiredName)
			{
				return true;
			}
		}

		return Blueprint->SkeletonGeneratedClass && FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, DesiredName);
	}

	TArray<FString> GetBlueprintMemberVariableNames(UBlueprint* Blueprint)
	{
		TArray<FString> Names;
		if (!Blueprint)
		{
			return Names;
		}

		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			Names.Add(Variable.VarName.ToString());
		}
		Names.Sort();
		return Names;
	}

	struct FBulkComponentEdit
	{
		FString PropertyPath;
		TSharedPtr<FJsonValue> Value;
	};

	struct FResolvedPropertyPath
	{
		FEditPropertyChain Chain;
		FProperty* TopLevelProperty = nullptr;
		FProperty* LeafProperty = nullptr;
		void* LeafValuePtr = nullptr;
	};

	struct FPropertyPathSegment
	{
		FString Name;
		bool bHasIndex = false;
		int32 Index = INDEX_NONE;
	};

	struct FBulkApplyConfig
	{
		FString ComponentClassFilter;
		FString ComponentFilter;
		TArray<FBulkComponentEdit> Edits;
		bool bDryRun = false;
		bool bDeferCompile = false;
		bool bIncludeInherited = false;
	};

	FString JsonValueToImportText(const TSharedPtr<FJsonValue>& JsonValue)
	{
		if (!JsonValue.IsValid() || JsonValue->IsNull())
		{
			return TEXT("None");
		}
		switch (JsonValue->Type)
		{
			case EJson::String: return JsonValue->AsString();
			case EJson::Number: return FString::SanitizeFloat(JsonValue->AsNumber());
			case EJson::Boolean: return JsonValue->AsBool() ? TEXT("True") : TEXT("False");
			default: break;
		}
		FString Serialized;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
		FJsonSerializer::Serialize(JsonValue.ToSharedRef(), TEXT(""), Writer);
		return Serialized;
	}

	bool ParsePropertyPathSegment(const FString& SegmentText, FPropertyPathSegment& OutSegment, FString& OutError)
	{
		OutSegment = FPropertyPathSegment();
		const int32 BracketStart = SegmentText.Find(TEXT("["), ESearchCase::CaseSensitive);
		if (BracketStart == INDEX_NONE)
		{
			OutSegment.Name = SegmentText;
		}
		else
		{
			if (!SegmentText.EndsWith(TEXT("]")))
			{
				OutError = FString::Printf(TEXT("Invalid indexed property segment '%s'"), *SegmentText);
				return false;
			}
			OutSegment.Name = SegmentText.Left(BracketStart);
			const FString IndexText = SegmentText.Mid(BracketStart + 1, SegmentText.Len() - BracketStart - 2);
			if (!LexTryParseString(OutSegment.Index, *IndexText) || OutSegment.Index < 0)
			{
				OutError = FString::Printf(TEXT("Invalid array index in property segment '%s'"), *SegmentText);
				return false;
			}
			OutSegment.bHasIndex = true;
		}

		if (OutSegment.Name.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Invalid empty property segment in '%s'"), *SegmentText);
			return false;
		}
		return true;
	}

	bool ResolveComponentPropertyPath(UActorComponent* Component, const FString& PropertyPath, FResolvedPropertyPath& OutResolved, FString& OutError)
	{
		if (!Component)
		{
			OutError = TEXT("Invalid component");
			return false;
		}

		TArray<FString> SegmentTexts;
		PropertyPath.ParseIntoArray(SegmentTexts, TEXT("."), true);
		if (SegmentTexts.Num() == 0)
		{
			OutError = TEXT("property_path is empty");
			return false;
		}

		void* Container = Component;
		UStruct* CurrentStruct = Component->GetClass();
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentTexts.Num(); ++SegmentIndex)
		{
			FPropertyPathSegment Segment;
			if (!ParsePropertyPathSegment(SegmentTexts[SegmentIndex], Segment, OutError))
			{
				return false;
			}

			if (!CurrentStruct)
			{
				OutError = FString::Printf(TEXT("Cannot resolve '%s' after non-struct property in '%s'"), *Segment.Name, *PropertyPath);
				return false;
			}

			FProperty* Property = CurrentStruct->FindPropertyByName(FName(*Segment.Name));
			if (!Property)
			{
				OutError = FString::Printf(TEXT("Property not found: '%s' on '%s'"), *Segment.Name, *CurrentStruct->GetName());
				return false;
			}

			OutResolved.Chain.AddTail(Property);
			if (!OutResolved.TopLevelProperty)
			{
				OutResolved.TopLevelProperty = Property;
			}
			OutResolved.LeafProperty = Property;
			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				if (!Segment.bHasIndex)
				{
					if (SegmentIndex != SegmentTexts.Num() - 1)
					{
						OutError = FString::Printf(TEXT("Array property '%s' requires an index"), *Segment.Name);
						return false;
					}
					OutResolved.LeafValuePtr = ValuePtr;
					CurrentStruct = nullptr;
					Container = ValuePtr;
					continue;
				}

				FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
				if (!ArrayHelper.IsValidIndex(Segment.Index))
				{
					OutError = FString::Printf(TEXT("Array index %d out of range for '%s' (num=%d)"), Segment.Index, *Segment.Name, ArrayHelper.Num());
					return false;
				}

				void* ElementPtr = ArrayHelper.GetRawPtr(Segment.Index);
				if (FStructProperty* InnerStructProperty = CastField<FStructProperty>(ArrayProperty->Inner))
				{
					OutResolved.Chain.AddTail(ArrayProperty->Inner);
					OutResolved.LeafProperty = ArrayProperty->Inner;
					OutResolved.LeafValuePtr = ElementPtr;
					CurrentStruct = InnerStructProperty->Struct;
					Container = ElementPtr;
				}
				else
				{
					OutResolved.Chain.AddTail(ArrayProperty->Inner);
					OutResolved.LeafProperty = ArrayProperty->Inner;
					OutResolved.LeafValuePtr = ElementPtr;
					CurrentStruct = nullptr;
					Container = ElementPtr;
				}
				continue;
			}

			if (Segment.bHasIndex)
			{
				OutError = FString::Printf(TEXT("Property '%s' is not an array"), *Segment.Name);
				return false;
			}

			OutResolved.LeafValuePtr = ValuePtr;
			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				CurrentStruct = StructProperty->Struct;
				Container = ValuePtr;
			}
			else
			{
				CurrentStruct = nullptr;
				Container = ValuePtr;
			}
		}

		if (!OutResolved.TopLevelProperty || !OutResolved.LeafProperty || !OutResolved.LeafValuePtr)
		{
			OutError = FString::Printf(TEXT("Failed to resolve property_path '%s'"), *PropertyPath);
			return false;
		}
		return true;
	}

	FString ExportPropertyValue(FProperty* Property, void* ValuePtr, UObject* Owner)
	{
		FString Value;
		if (Property && ValuePtr)
		{
			Property->ExportTextItem_Direct(Value, ValuePtr, nullptr, Owner, PPF_None);
		}
		return Value;
	}

	bool ImportPropertyValueWithNotify(UObject* Object, FResolvedPropertyPath& Resolved, const FString& TextValue, FString& OutError)
	{
		if (!Object || !Resolved.TopLevelProperty || !Resolved.LeafProperty || !Resolved.LeafValuePtr)
		{
			OutError = TEXT("Invalid resolved property path");
			return false;
		}

		Resolved.Chain.SetActivePropertyNode(Resolved.LeafProperty);
		Resolved.Chain.SetActiveMemberPropertyNode(Resolved.TopLevelProperty);
		Object->PreEditChange(Resolved.Chain);
		FOutputDeviceNull ErrorDevice;
		const TCHAR* ImportResult = Resolved.LeafProperty->ImportText_Direct(*TextValue, Resolved.LeafValuePtr, Object, PPF_None, &ErrorDevice);
		if (!ImportResult)
		{
			OutError = FString::Printf(TEXT("Failed to import value '%s' into property '%s'"), *TextValue, *Resolved.LeafProperty->GetName());
			return false;
		}
		FPropertyChangedEvent Event(Resolved.LeafProperty, EPropertyChangeType::ValueSet, MakeArrayView((const UObject* const*)&Object, 1));
		Event.SetActiveMemberProperty(Resolved.TopLevelProperty);
		FPropertyChangedChainEvent ChainEvent(Resolved.Chain, Event);
		Object->PostEditChangeChainProperty(ChainEvent);
		return true;
	}

	bool ComponentMatchesClassFilter(UActorComponent* Component, const FString& ComponentClassFilter)
	{
		if (!Component || ComponentClassFilter.IsEmpty())
		{
			return true;
		}
		for (UClass* Class = Component->GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (Class->GetName().Equals(ComponentClassFilter, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	void CollectOwnSCSVariableNames(UBlueprint* Blueprint, TSet<FName>& OutNames)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			return;
		}
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node)
			{
				OutNames.Add(Node->GetVariableName());
			}
		}
	}

	bool ComponentMatchesNameFilter(UActorComponent* Component, const FString& ComponentName, const FString& ComponentFilter)
	{
		return ComponentFilter.IsEmpty()
			|| ComponentName.Equals(ComponentFilter, ESearchCase::IgnoreCase)
			|| (Component && Component->GetName().Equals(ComponentFilter, ESearchCase::IgnoreCase));
	}

	void CollectInheritedBPScsNodes(UBlueprint* ChildBlueprint, TArray<USCS_Node*>& OutNodes)
	{
		OutNodes.Reset();
		if (!ChildBlueprint)
		{
			return;
		}

		TSet<FName> ShadowedNames;
		CollectOwnSCSVariableNames(ChildBlueprint, ShadowedNames);

		for (UBlueprintGeneratedClass* ParentBPGC = Cast<UBlueprintGeneratedClass>(ChildBlueprint->ParentClass);
			ParentBPGC;
			ParentBPGC = Cast<UBlueprintGeneratedClass>(ParentBPGC->GetSuperClass()))
		{
			USimpleConstructionScript* ParentSCS = ParentBPGC->SimpleConstructionScript;
			if (!ParentSCS)
			{
				continue;
			}

			for (USCS_Node* ParentNode : ParentSCS->GetAllNodes())
			{
				if (!ParentNode || ShadowedNames.Contains(ParentNode->GetVariableName()))
				{
					continue;
				}
				OutNodes.Add(ParentNode);
			}
		}
	}

	bool ParseMaterialPropertyPath(const FString& PropertyPath, int32& OutIndex)
	{
		FPropertyPathSegment Segment;
		FString Error;
		if (!ParsePropertyPathSegment(PropertyPath, Segment, Error) || !Segment.bHasIndex)
		{
			return false;
		}
		if (!Segment.Name.Equals(TEXT("Material"), ESearchCase::IgnoreCase) && !Segment.Name.Equals(TEXT("Materials"), ESearchCase::IgnoreCase))
		{
			return false;
		}
		OutIndex = Segment.Index;
		return true;
	}

	UClass* ResolveChildActorClassForBulk(const FString& ClassPath)
	{
		UClass* ChildClass = nullptr;
		UBlueprint* ChildBP = LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(ClassPath));
		if (ChildBP)
		{
			if (ChildBP->Status == BS_Dirty || ChildBP->Status == BS_Unknown)
			{
				FKismetEditorUtilities::CompileBlueprint(ChildBP, EBlueprintCompileOptions::SkipGarbageCollection);
			}
			ChildClass = ChildBP->GeneratedClass;
		}
		else
		{
			ChildClass = LoadObject<UClass>(nullptr, *NormalizeObjectPath(ClassPath));
			if (!ChildClass)
			{
				FString GeneratedClassPath = ClassPath;
				if (!GeneratedClassPath.EndsWith(TEXT("_C")))
				{
					GeneratedClassPath += TEXT("_C");
				}
				ChildClass = LoadObject<UClass>(nullptr, *NormalizeObjectPath(GeneratedClassPath));
			}
		}
		return ChildClass;
	}

	bool ApplyBulkSpecialEdit(UActorComponent* Component, const FString& PropertyPath, const FString& TextValue, bool bDryRun, FString& InOutBefore, FString& InOutAfter, FString& OutError, bool& bOutHandled)
	{
		bOutHandled = true;
		if (PropertyPath.Equals(TEXT("Collision.ObjectType"), ESearchCase::IgnoreCase))
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive) { OutError = TEXT("Collision.ObjectType requires a PrimitiveComponent"); return false; }
			InOutBefore = CollisionChannelDisplayName(Primitive->GetCollisionObjectType());
			ECollisionChannel Channel = ECC_WorldStatic;
			if (!ResolveCollisionChannelByName(TextValue, Channel)) { OutError = FString::Printf(TEXT("Unknown collision object type '%s'"), *TextValue); return false; }
			if (!bDryRun)
			{
				Primitive->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
				Primitive->SetCollisionObjectType(Channel);
			}
			InOutAfter = bDryRun ? TextValue : CollisionChannelDisplayName(Primitive->GetCollisionObjectType());
			return true;
		}

		if (PropertyPath.StartsWith(TEXT("Collision.Response."), ESearchCase::IgnoreCase))
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive) { OutError = TEXT("Collision.Response requires a PrimitiveComponent"); return false; }
			const FString ChannelName = PropertyPath.Mid(19);
			ECollisionChannel Channel = ECC_WorldStatic;
			if (!ResolveCollisionChannelByName(ChannelName, Channel)) { OutError = FString::Printf(TEXT("Unknown response channel '%s'"), *ChannelName); return false; }
			ECollisionResponse Response = ECR_Block;
			if (!ParseCollisionResponse(TextValue, Response)) { OutError = FString::Printf(TEXT("Invalid response '%s' for channel '%s' (use Ignore/Overlap/Block)"), *TextValue, *ChannelName); return false; }
			InOutBefore = CollisionResponseName(Primitive->GetCollisionResponseToChannel(Channel));
			if (!bDryRun)
			{
				Primitive->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
				Primitive->SetCollisionResponseToChannel(Channel, Response);
			}
			InOutAfter = bDryRun ? CollisionResponseName(Response) : CollisionResponseName(Primitive->GetCollisionResponseToChannel(Channel));
			return true;
		}

		if (PropertyPath.Equals(TEXT("Collision.Profile"), ESearchCase::IgnoreCase))
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive) { OutError = TEXT("Collision.Profile requires a PrimitiveComponent"); return false; }
			InOutBefore = Primitive->GetCollisionProfileName().ToString();
			if (!bDryRun)
			{
				Primitive->SetCollisionProfileName(FName(*TextValue));
			}
			InOutAfter = bDryRun ? TextValue : Primitive->GetCollisionProfileName().ToString();
			return true;
		}

		if (PropertyPath.StartsWith(TEXT("Collision.Preset."), ESearchCase::IgnoreCase))
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive) { OutError = TEXT("Collision.Preset requires a PrimitiveComponent"); return false; }
			const FString ObjectType = PropertyPath.Mid(17);
			InOutBefore = CollisionChannelDisplayName(Primitive->GetCollisionObjectType());
			ECollisionChannel Channel = ECC_WorldStatic;
			if (!ResolveCollisionChannelByName(ObjectType, Channel)) { OutError = FString::Printf(TEXT("Unknown collision preset object type '%s'"), *ObjectType); return false; }
			if (!bDryRun)
			{
				Primitive->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
				Primitive->SetCollisionObjectType(Channel);
			}
			InOutAfter = bDryRun ? ObjectType : CollisionChannelDisplayName(Primitive->GetCollisionObjectType());
			return true;
		}

		if (PropertyPath.Equals(TEXT("StaticMesh"), ESearchCase::IgnoreCase))
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
			if (!StaticMeshComponent) { OutError = TEXT("StaticMesh requires a StaticMeshComponent"); return false; }
			InOutBefore = StaticMeshComponent->GetStaticMesh() ? StaticMeshComponent->GetStaticMesh()->GetPathName() : TEXT("None");
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *NormalizeObjectPath(TextValue));
			if (!Mesh) { OutError = FString::Printf(TEXT("StaticMesh not found: '%s'"), *TextValue); return false; }
			if (!bDryRun)
			{
				StaticMeshComponent->SetStaticMesh(Mesh);
			}
			InOutAfter = bDryRun ? Mesh->GetPathName() : (StaticMeshComponent->GetStaticMesh() ? StaticMeshComponent->GetStaticMesh()->GetPathName() : TEXT("None"));
			return true;
		}

		int32 MaterialIndex = INDEX_NONE;
		if (ParseMaterialPropertyPath(PropertyPath, MaterialIndex))
		{
			UMeshComponent* MeshComponent = Cast<UMeshComponent>(Component);
			if (!MeshComponent) { OutError = TEXT("Material[i] requires a MeshComponent"); return false; }
			InOutBefore = MeshComponent->GetMaterial(MaterialIndex) ? MeshComponent->GetMaterial(MaterialIndex)->GetPathName() : TEXT("None");
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *NormalizeObjectPath(TextValue));
			if (!Material) { OutError = FString::Printf(TEXT("Material not found: '%s'"), *TextValue); return false; }
			if (!bDryRun)
			{
				MeshComponent->SetMaterial(MaterialIndex, Material);
			}
			InOutAfter = bDryRun ? Material->GetPathName() : (MeshComponent->GetMaterial(MaterialIndex) ? MeshComponent->GetMaterial(MaterialIndex)->GetPathName() : TEXT("None"));
			return true;
		}

		if (PropertyPath.Equals(TEXT("PostProcessMaterial"), ESearchCase::IgnoreCase))
		{
			FPostProcessSettings* PPSettings = nullptr;
			float* BlendWeightPtr = nullptr;
			if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
			{
				PPSettings = &Camera->PostProcessSettings;
				BlendWeightPtr = &Camera->PostProcessBlendWeight;
			}
			else if (UPostProcessComponent* PPComp = Cast<UPostProcessComponent>(Component))
			{
				PPSettings = &PPComp->Settings;
				BlendWeightPtr = &PPComp->BlendWeight;
			}
			if (!PPSettings) { OutError = TEXT("PostProcessMaterial requires a CameraComponent or PostProcessComponent"); return false; }
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *NormalizeObjectPath(TextValue));
			if (!Material) { OutError = FString::Printf(TEXT("Material not found: '%s'"), *TextValue); return false; }
			InOutBefore = FString::FromInt(PPSettings->WeightedBlendables.Array.Num());
			if (!bDryRun)
			{
				FWeightedBlendable Entry;
				Entry.Weight = 1.0f;
				Entry.Object = Material;
				PPSettings->WeightedBlendables.Array.Add(Entry);
				if (BlendWeightPtr) { *BlendWeightPtr = 1.0f; }
			}
			InOutAfter = FString::FromInt(bDryRun ? PPSettings->WeightedBlendables.Array.Num() + 1 : PPSettings->WeightedBlendables.Array.Num());
			return true;
		}

		if (PropertyPath.Equals(TEXT("ChildActorClass"), ESearchCase::IgnoreCase))
		{
			UChildActorComponent* ChildActorComp = Cast<UChildActorComponent>(Component);
			if (!ChildActorComp) { OutError = FString::Printf(TEXT("Component '%s' is not a ChildActorComponent"), *Component->GetName()); return false; }
			UClass* ChildClass = ResolveChildActorClassForBulk(TextValue);
			if (!ChildClass) { OutError = FString::Printf(TEXT("Failed to resolve ChildActorClass: '%s'"), *TextValue); return false; }
			InOutBefore = ChildActorComp->GetChildActorClass() ? ChildActorComp->GetChildActorClass()->GetPathName() : TEXT("None");
			if (!bDryRun)
			{
				ChildActorComp->SetChildActorClass(ChildClass);
			}
			InOutAfter = bDryRun ? ChildClass->GetPathName() : (ChildActorComp->GetChildActorClass() ? ChildActorComp->GetChildActorClass()->GetPathName() : TEXT("None"));
			return true;
		}

		bOutHandled = false;
		return false;
	}

	bool ApplyBulkEdit(UActorComponent* Component, const FBulkComponentEdit& Edit, bool bDryRun, TSharedPtr<FJsonObject>& OutEditResult)
	{
		OutEditResult = MakeShared<FJsonObject>();
		OutEditResult->SetStringField(TEXT("property_path"), Edit.PropertyPath);
		const FString TextValue = JsonValueToImportText(Edit.Value);

		FString Before;
		FString After;
		FString Error;
		bool bHandled = false;
		if (ApplyBulkSpecialEdit(Component, Edit.PropertyPath, TextValue, bDryRun, Before, After, Error, bHandled))
		{
			OutEditResult->SetStringField(TEXT("before"), Before);
			OutEditResult->SetStringField(TEXT("after"), After);
			OutEditResult->SetStringField(TEXT("action"), bDryRun ? TEXT("would_change") : TEXT("applied"));
			OutEditResult->SetBoolField(TEXT("changed"), Before != After);
			return true;
		}

		if (bHandled)
		{
			OutEditResult->SetStringField(TEXT("action"), TEXT("error"));
			OutEditResult->SetStringField(TEXT("error"), Error);
			return false;
		}

		FResolvedPropertyPath Resolved;
		if (!ResolveComponentPropertyPath(Component, Edit.PropertyPath, Resolved, Error))
		{
			OutEditResult->SetStringField(TEXT("action"), TEXT("error"));
			OutEditResult->SetStringField(TEXT("error"), Error);
			return false;
		}

		Before = ExportPropertyValue(Resolved.LeafProperty, Resolved.LeafValuePtr, Component);
		if (!bDryRun)
		{
			if (!ImportPropertyValueWithNotify(Component, Resolved, TextValue, Error))
			{
				OutEditResult->SetStringField(TEXT("before"), Before);
				OutEditResult->SetStringField(TEXT("action"), TEXT("error"));
				OutEditResult->SetStringField(TEXT("error"), Error);
				return false;
			}
		}
		After = bDryRun ? TextValue : ExportPropertyValue(Resolved.LeafProperty, Resolved.LeafValuePtr, Component);

		OutEditResult->SetStringField(TEXT("before"), Before);
		OutEditResult->SetStringField(TEXT("after"), After);
		OutEditResult->SetStringField(TEXT("action"), bDryRun ? TEXT("would_change") : TEXT("applied"));
		OutEditResult->SetBoolField(TEXT("changed"), Before != After);
		return true;
	}

	TSharedPtr<FJsonObject> ProcessBlueprintBulkComponentProperties(const FString& BpPath, const FBulkApplyConfig& Cfg, TArray<UBlueprint*>& OutDeferredCompileBlueprints, int32& OutTotalBlueprintsChanged, int32& OutTotalComponentsChanged, int32& OutTotalEditsApplied)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("bp_path"), BpPath);
		TArray<TSharedPtr<FJsonValue>> ComponentResults;

		UBlueprint* Blueprint = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			Result->SetStringField(TEXT("status"), TEXT("error"));
			Result->SetStringField(TEXT("error"), TEXT("Invalid blueprint or no SimpleConstructionScript"));
			Result->SetNumberField(TEXT("changed"), 0);
			Result->SetNumberField(TEXT("skipped"), 0);
			Result->SetArrayField(TEXT("components"), ComponentResults);
			return Result;
		}

		bool bAnyApplied = false;
		int32 ComponentsChanged = 0;
		int32 ComponentsSkipped = 0;
		int32 EditsApplied = 0;
		int32 InheritedOverridesCreated = 0;
		int32 InheritedOverridesReused = 0;
		const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpBulkSetCompProp", "SmithUE: Bulk Set BP Component Properties"));

		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate)
			{
				continue;
			}
			UActorComponent* Component = Node->ComponentTemplate;
			const FString ComponentName = Node->GetVariableName().ToString();
			if (!Cfg.ComponentFilter.IsEmpty() && !ComponentName.Equals(Cfg.ComponentFilter, ESearchCase::IgnoreCase) && !Component->GetName().Equals(Cfg.ComponentFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!ComponentMatchesClassFilter(Component, Cfg.ComponentClassFilter))
			{
				continue;
			}

			TSharedPtr<FJsonObject> ComponentResult = MakeShared<FJsonObject>();
			ComponentResult->SetStringField(TEXT("name"), ComponentName);
			ComponentResult->SetStringField(TEXT("class"), Component->GetClass()->GetName());
			TArray<TSharedPtr<FJsonValue>> EditResults;
			bool bComponentApplied = false;

			for (const FBulkComponentEdit& Edit : Cfg.Edits)
			{
				if (!Cfg.bDryRun)
				{
					Blueprint->Modify();
					Component->Modify();
				}

				TSharedPtr<FJsonObject> EditResult;
				const bool bApplied = ApplyBulkEdit(Component, Edit, Cfg.bDryRun, EditResult);
				if (bApplied)
				{
					++EditsApplied;
					bComponentApplied = true;
					bAnyApplied = bAnyApplied || !Cfg.bDryRun;
				}
				EditResults.Add(MakeShared<FJsonValueObject>(EditResult));
			}

			ComponentResult->SetArrayField(TEXT("edits"), EditResults);
			ComponentResult->SetStringField(TEXT("action"), bComponentApplied ? (Cfg.bDryRun ? TEXT("would_change") : TEXT("changed")) : TEXT("error"));
			if (bComponentApplied)
			{
				++ComponentsChanged;
			}
			else
			{
				++ComponentsSkipped;
			}
			ComponentResults.Add(MakeShared<FJsonValueObject>(ComponentResult));
		}

		bool bInheritedOverrideCreated = false;
		if (Cfg.bIncludeInherited)
		{
			UBlueprintGeneratedClass* ChildBPGC = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
			TArray<USCS_Node*> InheritedNodes;
			CollectInheritedBPScsNodes(Blueprint, InheritedNodes);
			UInheritableComponentHandler* ReadOnlyICH = Blueprint->GetInheritableComponentHandler(false);

			for (USCS_Node* ParentNode : InheritedNodes)
			{
				if (!ParentNode || !ParentNode->ComponentTemplate)
				{
					continue;
				}

				const FString ComponentName = ParentNode->GetVariableName().ToString();
				FComponentKey Key(ParentNode);
				if (!Key.IsValid())
				{
					TSharedPtr<FJsonObject> ComponentResult = MakeShared<FJsonObject>();
					ComponentResult->SetStringField(TEXT("name"), ComponentName);
					ComponentResult->SetStringField(TEXT("class"), ParentNode->ComponentClass ? ParentNode->ComponentClass->GetName() : TEXT("Unknown"));
					ComponentResult->SetStringField(TEXT("source"), TEXT("inherited_override"));
					ComponentResult->SetStringField(TEXT("action"), TEXT("skipped"));
					ComponentResult->SetStringField(TEXT("reason"), TEXT("native inherited component not supported"));
					ComponentResults.Add(MakeShared<FJsonValueObject>(ComponentResult));
					++ComponentsSkipped;
					continue;
				}

				UActorComponent* ExistingOverride = ReadOnlyICH ? ReadOnlyICH->GetOverridenComponentTemplate(Key) : nullptr;
				UActorComponent* Component = ExistingOverride;
				if (!Component)
				{
					Component = Cfg.bDryRun
						? (ChildBPGC ? ParentNode->GetActualComponentTemplate(ChildBPGC) : ParentNode->ComponentTemplate.Get())
						: nullptr;
				}
				if (!Component)
				{
					Component = ParentNode->ComponentTemplate;
				}

				if (!ComponentMatchesNameFilter(Component, ComponentName, Cfg.ComponentFilter) || !ComponentMatchesClassFilter(Component, Cfg.ComponentClassFilter))
				{
					continue;
				}

				TSharedPtr<FJsonObject> ComponentResult = MakeShared<FJsonObject>();
				ComponentResult->SetStringField(TEXT("name"), ComponentName);
				ComponentResult->SetStringField(TEXT("class"), Component->GetClass()->GetName());
				ComponentResult->SetStringField(TEXT("source"), TEXT("inherited_override"));
				ComponentResult->SetStringField(TEXT("override"), ExistingOverride ? TEXT("reused") : TEXT("created"));
				TArray<TSharedPtr<FJsonValue>> EditResults;
				bool bComponentApplied = false;

				if (!Cfg.bDryRun)
				{
					UInheritableComponentHandler* ICH = Blueprint->GetInheritableComponentHandler(true);
					if (!ICH)
					{
						ComponentResult->SetStringField(TEXT("action"), TEXT("error"));
						ComponentResult->SetStringField(TEXT("error"), TEXT("Failed to create InheritableComponentHandler"));
						ComponentResults.Add(MakeShared<FJsonValueObject>(ComponentResult));
						++ComponentsSkipped;
						continue;
					}

					ICH->SetFlags(RF_Transactional);
					Component = ExistingOverride ? ExistingOverride : ICH->CreateOverridenComponentTemplate(Key);
					if (!Component)
					{
						ComponentResult->SetStringField(TEXT("action"), TEXT("error"));
						ComponentResult->SetStringField(TEXT("error"), TEXT("Failed to create inherited component override template"));
						ComponentResults.Add(MakeShared<FJsonValueObject>(ComponentResult));
						++ComponentsSkipped;
						continue;
					}

					if (!ExistingOverride)
					{
						bInheritedOverrideCreated = true;
					}
					ICH->Modify();
					Component->SetFlags(RF_Transactional);
					Component->Modify();
					Blueprint->Modify();
				}

				for (const FBulkComponentEdit& Edit : Cfg.Edits)
				{
					TSharedPtr<FJsonObject> EditResult;
					const bool bApplied = ApplyBulkEdit(Component, Edit, Cfg.bDryRun, EditResult);
					if (bApplied)
					{
						++EditsApplied;
						bComponentApplied = true;
						bAnyApplied = bAnyApplied || !Cfg.bDryRun;
					}
					EditResults.Add(MakeShared<FJsonValueObject>(EditResult));
				}

				ComponentResult->SetArrayField(TEXT("edits"), EditResults);
				ComponentResult->SetStringField(TEXT("action"), bComponentApplied ? (Cfg.bDryRun ? TEXT("would_change") : TEXT("changed")) : TEXT("error"));
				if (bComponentApplied)
				{
					++ComponentsChanged;
					if (ExistingOverride)
					{
						++InheritedOverridesReused;
					}
					else
					{
						++InheritedOverridesCreated;
					}
				}
				else
				{
					++ComponentsSkipped;
				}
				ComponentResults.Add(MakeShared<FJsonValueObject>(ComponentResult));
			}
		}

		if (bAnyApplied)
		{
			if (bInheritedOverrideCreated)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}
			else
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			}
			Blueprint->MarkPackageDirty();
			if (Cfg.bDeferCompile)
			{
				FBlueprintCompilationManager::QueueForCompilation(Blueprint);
				OutDeferredCompileBlueprints.AddUnique(Blueprint);
			}
			else
			{
				TArray<FString> CompileErrors;
				FSmithUEBpAtomicAPI::CompileBlueprint(Blueprint, CompileErrors, true);
			}
			++OutTotalBlueprintsChanged;
		}

		OutTotalComponentsChanged += ComponentsChanged;
		OutTotalEditsApplied += EditsApplied;
		Result->SetStringField(TEXT("status"), TEXT("success"));
		Result->SetNumberField(TEXT("changed"), ComponentsChanged);
		Result->SetNumberField(TEXT("skipped"), ComponentsSkipped);
		Result->SetNumberField(TEXT("edits_applied"), EditsApplied);
		Result->SetNumberField(TEXT("inherited_overrides_created"), InheritedOverridesCreated);
		Result->SetNumberField(TEXT("inherited_overrides_reused"), InheritedOverridesReused);
		Result->SetStringField(TEXT("note"), FString::Printf(TEXT("Inherited component overrides: created %d, reused %d."), InheritedOverridesCreated, InheritedOverridesReused));
		Result->SetArrayField(TEXT("components"), ComponentResults);
		return Result;
	}

	FString MobilityToString(EComponentMobility::Type Mobility)
	{
		switch (Mobility)
		{
			case EComponentMobility::Static: return TEXT("Static");
			case EComponentMobility::Stationary: return TEXT("Stationary");
			case EComponentMobility::Movable: return TEXT("Movable");
			default: return TEXT("Unknown");
		}
	}

	FString CollisionEnabledToString(ECollisionEnabled::Type Enabled)
	{
		switch (Enabled)
		{
			case ECollisionEnabled::NoCollision: return TEXT("NoCollision");
			case ECollisionEnabled::QueryOnly: return TEXT("QueryOnly");
			case ECollisionEnabled::PhysicsOnly: return TEXT("PhysicsOnly");
			case ECollisionEnabled::QueryAndPhysics: return TEXT("QueryAndPhysics");
			default: return TEXT("Unknown");
		}
	}

	TSharedPtr<FJsonObject> ComponentTemplateToDescribeJson(UActorComponent* Component, const FString& ComponentName, const FString& Source, bool bInheritedUnverifiable)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), ComponentName);
		Result->SetStringField(TEXT("class"), Component ? Component->GetClass()->GetName() : TEXT("Unknown"));
		Result->SetStringField(TEXT("source"), Source);

		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		Result->SetStringField(TEXT("mobility"), SceneComponent ? MobilityToString(SceneComponent->Mobility) : TEXT(""));

		TSharedPtr<FJsonObject> Collision = MakeShared<FJsonObject>();
		if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
		{
			Collision->SetStringField(TEXT("profile"), PrimitiveComponent->BodyInstance.GetCollisionProfileName().ToString());
			Collision->SetStringField(TEXT("enabled"), CollisionEnabledToString(PrimitiveComponent->GetCollisionEnabled()));
		}
		else
		{
			Collision->SetStringField(TEXT("profile"), TEXT(""));
			Collision->SetStringField(TEXT("enabled"), TEXT(""));
		}
		Result->SetObjectField(TEXT("collision"), Collision);

		TArray<TSharedPtr<FJsonValue>> Materials;
		if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
		{
			UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
			if (StaticMesh)
			{
				for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
				{
					UMaterialInterface* Material = StaticMaterial.MaterialInterface;
					Materials.Add(MakeShared<FJsonValueString>(Material ? Material->GetPathName() : TEXT("")));
				}
				Result->SetStringField(TEXT("mesh"), StaticMesh->GetPathName());
			}
			else
			{
				Result->SetField(TEXT("mesh"), MakeShared<FJsonValueNull>());
			}
		}
		else
		{
			Result->SetField(TEXT("mesh"), MakeShared<FJsonValueNull>());
		}
		Result->SetArrayField(TEXT("materials"), Materials);
		Result->SetBoolField(TEXT("inherited_unverifiable"), bInheritedUnverifiable);
		return Result;
	}

	TSharedPtr<FJsonObject> InheritedPlaceholderToDescribeJson(USCS_Node* ParentNode)
	{
		const FString ComponentName = ParentNode ? ParentNode->GetVariableName().ToString() : TEXT("Unknown");
		TSharedPtr<FJsonObject> Result = ComponentTemplateToDescribeJson(ParentNode ? ParentNode->ComponentTemplate.Get() : nullptr, ComponentName, TEXT("inherited"), true);
		if (ParentNode && !ParentNode->ComponentTemplate && ParentNode->ComponentClass)
		{
			Result->SetStringField(TEXT("class"), ParentNode->ComponentClass->GetName());
		}
		return Result;
	}

	TSharedPtr<FJsonObject> DescribeBlueprintComponents(UBlueprint* Blueprint)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("bp_path"), Blueprint ? Blueprint->GetOutermost()->GetName() : TEXT(""));
		Result->SetStringField(TEXT("bp_name"), Blueprint ? Blueprint->GetName() : TEXT(""));
		Result->SetStringField(TEXT("parent_class"), Blueprint && Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT(""));

		TArray<TSharedPtr<FJsonValue>> Components;
		int32 OwnComponentCount = 0;
		TSet<UActorComponent*> CoveredInheritedOverrides;

		if (Blueprint && Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node)
				{
					continue;
				}
				++OwnComponentCount;
				Components.Add(MakeShared<FJsonValueObject>(ComponentTemplateToDescribeJson(Node->ComponentTemplate.Get(), Node->GetVariableName().ToString(), TEXT("own"), false)));
			}
		}

		if (Blueprint)
		{
			UInheritableComponentHandler* ICH = Blueprint->GetInheritableComponentHandler(false);
			TArray<USCS_Node*> InheritedNodes;
			CollectInheritedBPScsNodes(Blueprint, InheritedNodes);

			for (USCS_Node* ParentNode : InheritedNodes)
			{
				if (!ParentNode)
				{
					continue;
				}

				UActorComponent* OverrideTemplate = nullptr;
				FComponentKey Key(ParentNode);
				if (ICH && Key.IsValid())
				{
					OverrideTemplate = ICH->GetOverridenComponentTemplate(Key);
				}

				if (OverrideTemplate)
				{
					CoveredInheritedOverrides.Add(OverrideTemplate);
					Components.Add(MakeShared<FJsonValueObject>(ComponentTemplateToDescribeJson(OverrideTemplate, ParentNode->GetVariableName().ToString(), TEXT("inherited_override"), false)));
				}
				else
				{
					Components.Add(MakeShared<FJsonValueObject>(InheritedPlaceholderToDescribeJson(ParentNode)));
				}
			}

			if (ICH)
			{
				TArray<UActorComponent*> OverrideTemplates;
				ICH->GetAllTemplates(OverrideTemplates);
				for (UActorComponent* OverrideTemplate : OverrideTemplates)
				{
					if (!OverrideTemplate || CoveredInheritedOverrides.Contains(OverrideTemplate))
					{
						continue;
					}
					Components.Add(MakeShared<FJsonValueObject>(ComponentTemplateToDescribeJson(OverrideTemplate, OverrideTemplate->GetFName().ToString(), TEXT("inherited_override"), false)));
				}
			}
		}

		Result->SetNumberField(TEXT("component_count"), OwnComponentCount);
		Result->SetArrayField(TEXT("components"), Components);
		return Result;
	}

	void AppendBlueprintAssetsFromFolder(const FString& FolderPath, bool bRecursive, TArray<FString>& OutBpPaths)
	{
		// Normalize content-browser virtual path -> real package path (handles project + plugins).
		FString Folder;
		FSmithUECommonUtils::NormalizeContentBrowserPath(FolderPath, Folder);


		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = bRecursive;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);
		for (const FAssetData& Asset : Assets)
		{
			OutBpPaths.AddUnique(Asset.PackageName.ToString());
		}
	}

	bool DisconnectPinsImpl(UBlueprint* Blueprint, UEdGraph* Graph, const FString& GraphPath, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName, FString& OutError)
	{
		UEdGraphNode* SourceNode = ResolveNodeId(Graph, GraphPath, SourceNodeId, OutError);
		if (!SourceNode && !OutError.IsEmpty()) { return false; }
		UEdGraphNode* TargetNode = ResolveNodeId(Graph, GraphPath, TargetNodeId, OutError);
		if (!TargetNode && !OutError.IsEmpty()) { return false; }
		if (!SourceNode || !TargetNode) { OutError = TEXT("Source or target node not found"); return false; }

		TArray<FString> SourceSuggestions;
		UEdGraphPin* SourcePin = FindPin(SourceNode, SourcePinName, &SourceSuggestions);
		if (!SourcePin)
		{
			if (SourceSuggestions.Num() > 0)
			{
				TSharedPtr<FJsonObject> Err = FSmithUECommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Pin '%s' is ambiguous on node %s. Did you mean one of the suggestions?"),
						*SourcePinName, *SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
				AppendJsonStringArray(Err, TEXT("suggestions"), SourceSuggestions);
				Err->SetStringField(TEXT("hint"), TEXT("Use exact pin name from suggestions list"));
				OutError = FSmithUECommonUtils::SerializeJson(Err);
				return false;
			}
			OutError = TEXT("Source or target pin not found");
			return false;
		}

		TArray<FString> TargetSuggestions;
		UEdGraphPin* TargetPin = FindPin(TargetNode, TargetPinName, &TargetSuggestions);
		if (!TargetPin)
		{
			if (TargetSuggestions.Num() > 0)
			{
				TSharedPtr<FJsonObject> Err = FSmithUECommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Pin '%s' is ambiguous on node %s. Did you mean one of the suggestions?"),
						*TargetPinName, *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
				AppendJsonStringArray(Err, TEXT("suggestions"), TargetSuggestions);
				Err->SetStringField(TEXT("hint"), TEXT("Use exact pin name from suggestions list"));
				OutError = FSmithUECommonUtils::SerializeJson(Err);
				return false;
			}
			OutError = TEXT("Source or target pin not found");
			return false;
		}

		if (!SourcePin->LinkedTo.Contains(TargetPin)) { OutError = TEXT("Pins are not connected"); return false; }
		SourcePin->BreakLinkTo(TargetPin);
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		OutError.Reset();
		return true;
	}
}

void FSmithUEBpAtomicAPI::RegisterTools(FSmithUEToolRegistry& Registry)
{
	FSmithUEToolParam BpAddComponentClassParam(TEXT("component_class"), TEXT("string"), TEXT("Component class name"), true);
	BpAddComponentClassParam.SetExample(TEXT("StaticMeshComponent"));
	FSmithUEToolParam BpBulkComponentClassParam(TEXT("component_class"), TEXT("string"), TEXT("Optional component class filter by class/superclass name, e.g. StaticMeshComponent. Empty = all component classes."));
	BpBulkComponentClassParam.SetExample(TEXT("StaticMeshComponent"));
	FSmithUEToolParam BpBulkEditsParam(TEXT("edits"), TEXT("array"), TEXT("Required array of objects {property_path,value}. property_path supports dotted/indexed paths and semantic keys Collision.ObjectType, Collision.Response.<Channel>, Collision.Profile, StaticMesh, Material[i]/Materials[i], PostProcessMaterial, ChildActorClass."), true, FString(), TEXT("object"));
	BpBulkEditsParam.SetExample(TEXT("[{\"property_path\":\"RelativeLocation.Z\",\"value\":500}]"));
	FSmithUEToolParam BpCollisionObjectTypeParam(TEXT("object_type"), TEXT("string"), TEXT("Collision object type display name. Default 'Vehicle'."));
	BpCollisionObjectTypeParam.SetExample(TEXT("Vehicle"));
	FSmithUEToolParam BpCollisionResponsesParam(TEXT("responses"), TEXT("object"), TEXT("Map of channel display name -> response, e.g. {\"Pawn\":\"Ignore\"}. Response is Ignore/Overlap/Block."));
	BpCollisionResponsesParam.SetExample(TEXT("{\"Pawn\":\"Ignore\"}"));

	Registry.Register(FSmithUEToolSchema(TEXT("bp_create"), TEXT("Blueprint"), TEXT("Create a new Blueprint asset (asset-level: creates a new Blueprint ASSET at a package path; for adding a node inside a graph use bp_create_node)."), { FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Blueprint asset name"), true), FSmithUEToolParam(TEXT("parent_class"), TEXT("string"), TEXT("Parent class name"), true), FSmithUEToolParam(TEXT("save_path"), TEXT("string"), TEXT("Destination content path"), true) }), &HandleBpCreate);
	Registry.Register(FSmithUEToolSchema(TEXT("create_blueprint_interface"), TEXT("Blueprint"), TEXT("Create a Blueprint Interface asset (UInterface, BPTYPE_Interface). Add interface functions with bp_add_function; implement it on a Blueprint with bp_implement_interface."), { FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Interface asset name (convention: BPI_*)"), true), FSmithUEToolParam(TEXT("save_path"), TEXT("string"), TEXT("Destination content path"), true) }), &HandleBpCreateInterface);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_implement_interface"), TEXT("Blueprint"), TEXT("Add (implement) an interface on a Blueprint. interface_path = a Blueprint-interface asset path (e.g. /Game/BPI_Foo) or a native UInterface class name. Compiles the Blueprint afterwards."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Target Blueprint path"), true), FSmithUEToolParam(TEXT("interface_path"), TEXT("string"), TEXT("Interface asset path or native UInterface class name"), true) }), &HandleBpImplementInterface);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_function"), TEXT("Blueprint"), TEXT("Add a function graph to a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("New function name"), true), FSmithUEToolParam(TEXT("inputs"), TEXT("array"), TEXT("Optional input pin definitions"), false, FString(), TEXT("object")), FSmithUEToolParam(TEXT("outputs"), TEXT("array"), TEXT("Optional output pin definitions"), false, FString(), TEXT("object")) }), &HandleBpAddFunction);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_create_node"), TEXT("Blueprint"), TEXT("Create a node inside a Blueprint graph (in-graph: adds a node inside a Blueprint graph; for a new Blueprint ASSET use bp_create). Returns node ids that become stale after any graph mutation — re-run bp_describe_graph before reusing them."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("node_class"), TEXT("string"), TEXT("Node class name"), true), FSmithUEToolParam(TEXT("position"), TEXT("object"), TEXT("Optional {x,y} node position")), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("Function name or 'ClassName::FunctionName' for K2Node_CallFunction nodes. Omit for self-configuring subclasses that already set their own function (e.g. K2Node_GetEditorProperty)."), false), FSmithUEToolParam(TEXT("variable_name"), TEXT("string"), TEXT("Variable name for K2Node_VariableGet or K2Node_VariableSet nodes"), false), FSmithUEToolParam(TEXT("macro_path"), TEXT("string"), TEXT("Macro graph asset path for K2Node_MacroInstance nodes"), false), FSmithUEToolParam(TEXT("key"), TEXT("string"), TEXT("Input key name (e.g. 'W', 'Gamepad_LeftX') for K2Node_InputKey nodes"), false), FSmithUEToolParam(TEXT("input_action"), TEXT("string"), TEXT("InputAction asset path for K2Node_EnhancedInputAction nodes"), false), FSmithUEToolParam(TEXT("target_class"), TEXT("string"), TEXT("Target class for K2Node_DynamicCast nodes. Accepts a native class name or a /Game/... Blueprint asset path (resolves the generated _C class)."), false), FSmithUEToolParam(TEXT("owner_class"), TEXT("string"), TEXT("Optional owner class for K2Node_VariableGet/VariableSet when the variable belongs to a FOREIGN class (e.g. a variable on a Cast result). Accepts a native class name or a /Game/... Blueprint asset path. Omit to resolve against the current Blueprint (Self)."), false) }), &HandleBpCreateNode);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_connect_pins"), TEXT("Blueprint"), TEXT("Connect two Blueprint node pins (adds a wire between two pins; to remove an existing wire use bp_disconnect_pins)."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("source_node_id"), TEXT("string"), TEXT("Source node GUID"), true), FSmithUEToolParam(TEXT("source_pin"), TEXT("string"), TEXT("Source pin name"), true), FSmithUEToolParam(TEXT("target_node_id"), TEXT("string"), TEXT("Target node GUID"), true), FSmithUEToolParam(TEXT("target_pin"), TEXT("string"), TEXT("Target pin name"), true) }), &HandleBpConnectPins);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_disconnect_pins"), TEXT("Blueprint"), TEXT("Disconnect two Blueprint node pins (removes an existing wire between two pins; to add a new wire use bp_connect_pins)."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("source_node_id"), TEXT("string"), TEXT("Source node GUID or N-id"), true), FSmithUEToolParam(TEXT("source_pin"), TEXT("string"), TEXT("Source pin name"), true), FSmithUEToolParam(TEXT("target_node_id"), TEXT("string"), TEXT("Target node GUID or N-id"), true), FSmithUEToolParam(TEXT("target_pin"), TEXT("string"), TEXT("Target pin name"), true) }), &HandleBpDisconnectPins);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_set_pin_default"), TEXT("Blueprint"), TEXT("Set a Blueprint node pin default value"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"), true), FSmithUEToolParam(TEXT("pin_name"), TEXT("string"), TEXT("Pin name"), true), FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Default value string"), true) }), &HandleBpSetPinDefault);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_set_anim_node_property"), TEXT("Blueprint"), TEXT("MUTATES an AnimGraph UAnimGraphNode only: set an internal FAnimNode struct property by reflection (e.g. Sequence or PlayRate). Not for regular K2 nodes or state machines; use bp_read_anim_node first for valid property names."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("AnimBlueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("AnimGraph graph name as returned by bp_describe_graph/anim_read_blueprint"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("AnimGraph node GUID or fresh N-id"), true), FSmithUEToolParam(TEXT("property"), TEXT("string"), TEXT("Internal FAnimNode property name; run bp_read_anim_node to list valid names"), true), FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("ImportText value string, e.g. 1.0, True, or an asset reference"), true) }), &HandleBpSetAnimNodeProperty);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_expose_anim_pin"), TEXT("Blueprint"), TEXT("MUTATES an AnimGraph UAnimGraphNode only: show or hide one optional internal FAnimNode property pin. Not for regular Blueprint pins; ids can go stale after mutation — re-run bp_describe_graph/bp_read_anim_node."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("AnimBlueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("AnimGraph graph name"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("AnimGraph node GUID or fresh N-id"), true), FSmithUEToolParam(TEXT("property"), TEXT("string"), TEXT("Optional FAnimNode property pin name; run bp_read_anim_node to list valid names"), true), FSmithUEToolParam(TEXT("show"), TEXT("boolean"), TEXT("true to expose the property as a pin; false to hide it"), true) }), &HandleBpExposeAnimPin);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_bind_anim_property"), TEXT("Blueprint"), TEXT("MUTATES an AnimGraph UAnimGraphNode only: bind one anim node property to a MEMBER VARIABLE via PropertyBindings fast-path (no wire). variable must be a member variable name; empty variable unbinds. Not for functions, external objects, or state machines."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("AnimBlueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("AnimGraph graph name"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("AnimGraph node GUID or fresh N-id"), true), FSmithUEToolParam(TEXT("property"), TEXT("string"), TEXT("Bindable internal FAnimNode property name, e.g. PlayRate; run bp_read_anim_node first"), true), FSmithUEToolParam(TEXT("variable"), TEXT("string"), TEXT("Member variable name to bind; empty string removes existing binding"), true) }), &HandleBpBindAnimProperty);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_read_anim_node"), TEXT("Blueprint"), TEXT("READ-ONLY: inspect one AnimGraph UAnimGraphNode's internal FAnimNode settable properties, optional exposed pins, and PropertyBindings. Use before bp_set_anim_node_property/bp_expose_anim_pin/bp_bind_anim_property; not for regular K2 nodes or state machines."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("AnimBlueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("AnimGraph graph name"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("AnimGraph node GUID or fresh N-id"), true) }), &HandleBpReadAnimNode);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_state_machine"), TEXT("Blueprint"), TEXT("CREATE: AnimBlueprint AnimGraph state machines ONLY. Adds a UAnimGraphNode_StateMachine to an AnimGraph and returns node_id plus state_machine_graph for follow-up bp_add_anim_state; ids go stale after graph mutation — re-run bp_describe_graph/bp_read_state_machine."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("AnimBlueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target AnimGraph graph name (usually AnimGraph)"), true), FSmithUEToolParam(TEXT("position"), TEXT("object"), TEXT("Optional {x,y} node position")) }), &HandleBpAddStateMachine);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_anim_state"), TEXT("Blueprint"), TEXT("CREATE: AnimBlueprint state-machine graphs ONLY. Adds a UAnimStateNode with its UAnimationStateGraph BoundGraph; state_machine accepts state-machine node_id or graph name. First state is wired from Entry. Returns bound_graph for bp_create_node/bp_set_anim_node_property population; ids go stale."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("AnimBlueprint asset path"), true), FSmithUEToolParam(TEXT("state_machine"), TEXT("string"), TEXT("State machine node GUID/N-id or state machine graph name returned by bp_add_state_machine"), true), FSmithUEToolParam(TEXT("state_name"), TEXT("string"), TEXT("State name / BoundGraph rename suggestion"), true), FSmithUEToolParam(TEXT("position"), TEXT("object"), TEXT("Optional {x,y} state node position")) }), &HandleBpAddAnimState);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_anim_transition"), TEXT("Blueprint"), TEXT("CREATE: AnimBlueprint state-machine graphs ONLY. Adds a UAnimStateTransitionNode between two states with its UAnimationTransitionGraph rule BoundGraph. from_state/to_state accept state node_id or state name. Returns rule_graph for condition nodes; ids go stale."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("AnimBlueprint asset path"), true), FSmithUEToolParam(TEXT("state_machine"), TEXT("string"), TEXT("State machine node GUID/N-id or state machine graph name"), true), FSmithUEToolParam(TEXT("from_state"), TEXT("string"), TEXT("Source state node GUID/N-id or state name"), true), FSmithUEToolParam(TEXT("to_state"), TEXT("string"), TEXT("Target state node GUID/N-id or state name"), true), FSmithUEToolParam(TEXT("position"), TEXT("object"), TEXT("Optional {x,y} transition node position")) }), &HandleBpAddAnimTransition);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_read_state_machine"), TEXT("Blueprint"), TEXT("READ-ONLY pair for state-machine create tools: AnimBlueprint state machines ONLY. Reports states, transitions, Entry target, and every state/transition BoundGraph name; state_machine accepts node_id or graph name."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("AnimBlueprint asset path"), true), FSmithUEToolParam(TEXT("state_machine"), TEXT("string"), TEXT("State machine node GUID/N-id or state machine graph name"), true) }), &HandleBpReadStateMachine);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_delete_node"), TEXT("Blueprint"), TEXT("Delete a node from a Blueprint graph Returns node ids that become stale after any graph mutation — re-run bp_describe_graph before reusing them."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"), true) }), &HandleBpDeleteNode);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_variable"), TEXT("Blueprint"), TEXT("Add a Blueprint member variable"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("var_name"), TEXT("string"), TEXT("Variable name"), true), FSmithUEToolParam(TEXT("var_type"), TEXT("string"), TEXT("Variable type name"), true), FSmithUEToolParam(TEXT("default_value"), TEXT("string"), TEXT("Optional default value")), FSmithUEToolParam(TEXT("category"), TEXT("string"), TEXT("Optional category name")) }), &HandleBpAddVariable);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_remove_variable"), TEXT("Blueprint"), TEXT("Remove a Blueprint member variable by name"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("var_name"), TEXT("string"), TEXT("Variable name to remove"), true) }), &HandleBpRemoveVariable);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_rename_variable"), TEXT("Blueprint"), TEXT("Rename a Blueprint member variable and fix up all graph references (via FBlueprintEditorUtils::RenameMemberVariable). Recompiles the Blueprint."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("var_name"), TEXT("string"), TEXT("Current variable name"), true), FSmithUEToolParam(TEXT("new_name"), TEXT("string"), TEXT("New variable name"), true) }), &HandleBpRenameVariable);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_set_variable_flags"), TEXT("Blueprint"), TEXT("Set editor-facing flags/metadata on an existing Blueprint member variable: instance_editable (shows in the Details panel of instances), blueprint_read_only, expose_on_spawn (requires instance_editable), category, tooltip. Only the provided fields are changed."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("var_name"), TEXT("string"), TEXT("Variable name"), true), FSmithUEToolParam(TEXT("instance_editable"), TEXT("bool"), TEXT("Editable per-instance (not Blueprint-only)"), false), FSmithUEToolParam(TEXT("blueprint_read_only"), TEXT("bool"), TEXT("Read-only in Blueprint graphs"), false), FSmithUEToolParam(TEXT("expose_on_spawn"), TEXT("bool"), TEXT("Expose as a SpawnActor pin (needs instance_editable)"), false), FSmithUEToolParam(TEXT("category"), TEXT("string"), TEXT("Variable category"), false), FSmithUEToolParam(TEXT("tooltip"), TEXT("string"), TEXT("Variable tooltip"), false) }), &HandleBpSetVariableFlags);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_event_dispatcher"), TEXT("Blueprint"), TEXT("Create an Event Dispatcher (multicast delegate) on a Blueprint — the core event-communication primitive. Other graphs Bind/Assign to it and Call it to broadcast. Optionally give it a typed signature via params (array of {name, type}, e.g. [{\"name\":\"NewHealth\",\"type\":\"float\"}]). Recompiles the Blueprint."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("dispatcher_name"), TEXT("string"), TEXT("Event dispatcher name (convention: On*)"), true), FSmithUEToolParam(TEXT("params"), TEXT("array"), TEXT("Optional signature params: array of {name, type} (type like float/int/bool/string/vector/object:<Class>)"), false) }), &HandleBpAddEventDispatcher);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_remove_event_dispatcher"), TEXT("Blueprint"), TEXT("Remove an Event Dispatcher (and its signature graph) from a Blueprint by name. Recompiles the Blueprint."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("dispatcher_name"), TEXT("string"), TEXT("Event dispatcher name to remove"), true) }), &HandleBpRemoveEventDispatcher);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_function_parameter"), TEXT("Blueprint"), TEXT("Add an input or output parameter to an EXISTING Blueprint function (modifies its signature). direction=input adds to the function entry; direction=output adds to the return node (created if absent). Rejects a duplicate pin name. Recompiles."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("Existing function graph name"), true), FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Parameter name"), true), FSmithUEToolParam(TEXT("param_type"), TEXT("string"), TEXT("Parameter type (float/int/bool/string/vector/object:<Class>...)"), true), FSmithUEToolParam(TEXT("direction"), TEXT("string"), TEXT("input | output (default input)"), false).SetAllowedValues({ TEXT("input"), TEXT("output") }) }), &HandleBpAddFunctionParameter);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_remove_function_parameter"), TEXT("Blueprint"), TEXT("Remove a parameter (input or output) from an existing Blueprint function by name. Recompiles."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("Function graph name"), true), FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Parameter name to remove"), true) }), &HandleBpRemoveFunctionParameter);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_local_variable"), TEXT("Blueprint"), TEXT("Add a local variable to a Blueprint function graph (scoped to that function). Recompiles."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("Function graph name"), true), FSmithUEToolParam(TEXT("var_name"), TEXT("string"), TEXT("Local variable name"), true), FSmithUEToolParam(TEXT("var_type"), TEXT("string"), TEXT("Variable type (float/int/bool/string/vector/object:<Class>...)"), true), FSmithUEToolParam(TEXT("default_value"), TEXT("string"), TEXT("Optional default value"), false) }), &HandleBpAddLocalVariable);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_timeline"), TEXT("Blueprint"), TEXT("Add a Timeline to a Blueprint (creates the timeline template/component). Add float/vector/color/event tracks and drive it from the event graph with a Timeline node (bp_create_node K2Node_Timeline). Recompiles."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("timeline_name"), TEXT("string"), TEXT("Timeline name"), true) }), &HandleBpAddTimeline);
	  Registry.Register(FSmithUEToolSchema(TEXT("bp_add_component"), TEXT("Blueprint"), TEXT("Add a component to a Blueprint SCS"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), BpAddComponentClassParam, FSmithUEToolParam(TEXT("component"), TEXT("string"), TEXT("Component instance name"), true), FSmithUEToolParam(TEXT("static_mesh"), TEXT("string"), TEXT("Optional StaticMesh asset path for StaticMeshComponent"), false), FSmithUEToolParam(TEXT("parent"), TEXT("string"), TEXT("Optional parent component name to attach to"), false) }), &HandleBpAddComponent);
	  Registry.Register(FSmithUEToolSchema(TEXT("bp_remove_component"), TEXT("Blueprint"), TEXT("Remove a component from a Blueprint SCS"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("component"), TEXT("string"), TEXT("Component instance name to remove"), true) }), &HandleBpRemoveComponent);
	  Registry.Register(FSmithUEToolSchema(TEXT("bp_rename_component"), TEXT("Blueprint"), TEXT("Rename a Blueprint SCS component variable (updates all graph references)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("component"), TEXT("string"), TEXT("Current component name"), true), FSmithUEToolParam(TEXT("new_name"), TEXT("string"), TEXT("New component name"), true) }), &HandleBpRenameComponent);
	  Registry.Register(FSmithUEToolSchema(TEXT("bp_set_component_property"), TEXT("Blueprint"), TEXT("Set a property on a Blueprint SCS or inherited component template"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("component"), TEXT("string"), TEXT("Component name (SCS or inherited)"), true), FSmithUEToolParam(TEXT("property_name"), TEXT("string"), TEXT("Property name, or 'PostProcessMaterial' to add a blendable material"), true), FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Property value (string/number/bool), or material asset path for PostProcessMaterial"), true) }), &HandleBpSetComponentProperty);
		Registry.Register(FSmithUEToolSchema(TEXT("bp_describe_components"), TEXT("Blueprint"), TEXT("Read back the full component tree and spec-governed properties (collision/Mobility/materials) of one Blueprint (bp_path) or every Blueprint under a folder (folder_path). Inherited-component gaps are explicitly flagged as inherited_unverifiable."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Single Blueprint asset path (provide this OR folder_path).")), FSmithUEToolParam(TEXT("folder_path"), TEXT("string"), TEXT("Content folder path for batch mode (provide this OR bp_path).")), FSmithUEToolParam(TEXT("recursive"), TEXT("boolean"), TEXT("Recurse into sub-folders. Default false.")) }), &HandleBpDescribeComponents);
			Registry.Register(FSmithUEToolSchema(TEXT("bp_bulk_set_component_property"), TEXT("Blueprint"), TEXT("Bulk-set generic component template properties on own SCS components in one Blueprint (bp_path) or every Blueprint directly under a folder (folder_path, non-recursive). Supports dotted/indexed property_path (e.g. RelativeLocation.Z, BodyInstance.bSimulatePhysics, OverrideMaterials[0]) plus semantic setters for collision, StaticMesh, Material[i], PostProcessMaterial, and ChildActorClass. Set include_inherited=true to edit parent-Blueprint SCS inherited components as child ICH override templates."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Single Blueprint asset path. Provide this OR folder_path.")), FSmithUEToolParam(TEXT("folder_path"), TEXT("string"), TEXT("Content folder (e.g. /Game/Vehicles); applies to all Blueprints directly under it (non-recursive). Leading /All is stripped. Provide this OR bp_path.")), BpBulkComponentClassParam, FSmithUEToolParam(TEXT("component"), TEXT("string"), TEXT("Optional component variable/template name. Empty = all matching own SCS components.")), BpBulkEditsParam, FSmithUEToolParam(TEXT("include_inherited"), TEXT("boolean"), TEXT("Also target parent-Blueprint SCS inherited components by creating/reusing child InheritableComponentHandler override templates. Default false.")), FSmithUEToolParam(TEXT("dry_run"), TEXT("boolean"), TEXT("Preview changes without modifying or compiling. Default false.")), FSmithUEToolParam(TEXT("defer_compile"), TEXT("boolean"), TEXT("Queue changed Blueprints and flush compilation once at end. Default false.")) }), &HandleBpBulkSetComponentProperty);
		Registry.Register(FSmithUEToolSchema(TEXT("bp_set_component_collision"), TEXT("Blueprint"), TEXT("Bulk-set collision (object type + per-channel responses) on StaticMeshComponent templates inside one Blueprint (bp_path) or every Blueprint directly under a folder (folder_path, non-recursive). Switches the component to a Custom profile, then applies object type and responses via proper engine setters. Skips components whose StaticMesh has no collision geometry unless disabled."), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Single Blueprint asset path. Provide this OR folder_path.")), FSmithUEToolParam(TEXT("folder_path"), TEXT("string"), TEXT("Content folder (e.g. /Game/MyVehicles); applies to all Blueprints directly under it (non-recursive). Provide this OR bp_path.")), FSmithUEToolParam(TEXT("component"), TEXT("string"), TEXT("Optional: only this StaticMeshComponent name. Empty = all StaticMeshComponents.")), BpCollisionObjectTypeParam, BpCollisionResponsesParam, FSmithUEToolParam(TEXT("skip_if_no_mesh_collision"), TEXT("boolean"), TEXT("Skip a component if its StaticMesh asset has no collision geometry. Default true.")), FSmithUEToolParam(TEXT("dry_run"), TEXT("boolean"), TEXT("Preview changes without modifying. Default false.")) }), &HandleBpSetComponentCollision);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_override_function"), TEXT("Blueprint"), TEXT("Override a parent class function in a Blueprint (creates proper override graph with correct signature)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("Parent function name to override"), true) }), &HandleBpOverrideFunction);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_compile"), TEXT("Blueprint"), TEXT("Compile a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true) }), &HandleBpCompile);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_reparent"), TEXT("Blueprint"), TEXT("Change the parent class of a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("new_parent_class"), TEXT("string"), TEXT("New parent class name or Blueprint path"), true) }), &HandleBpReparent);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_copy_graph"), TEXT("Blueprint"), TEXT("Copy a function graph from one Blueprint to another"), { FSmithUEToolParam(TEXT("source_bp"), TEXT("string"), TEXT("Source Blueprint asset path"), true), FSmithUEToolParam(TEXT("target_bp"), TEXT("string"), TEXT("Target Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Function graph name to copy"), true), FSmithUEToolParam(TEXT("new_graph_name"), TEXT("string"), TEXT("Optional new name for the copied graph")), FSmithUEToolParam(TEXT("overwrite"), TEXT("boolean"), TEXT("If true, removes existing graph with same name before copying")) }), &HandleBpCopyGraph);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_remove_graph"), TEXT("Blueprint"), TEXT("Remove a function graph or ubergraph page from a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name to remove"), true) }), &HandleBpRemoveGraph);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_rename_graph"), TEXT("Blueprint"), TEXT("Rename a function graph or event graph page in a Blueprint (updates all internal call references)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Current graph name"), true), FSmithUEToolParam(TEXT("new_name"), TEXT("string"), TEXT("New graph name"), true) }), &HandleBpRenameGraph);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_fixup_self_references"), TEXT("Blueprint"), TEXT("Fix variable/function/component nodes to reference Self instead of a foreign parent class (use after bp_copy_graph across different class hierarchies)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true) }), &HandleBpFixupSelfReferences);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_fix_local_var_scope"), TEXT("Blueprint"), TEXT("Fix stale local variable scope references in all function graphs (use after bp_rename_graph or bp_copy_graph when local variables show scope mismatch warnings)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true) }), &HandleBpFixLocalVarScope);
}

UBlueprint* FSmithUEBpAtomicAPI::LoadBlueprint(const FString& BpPath)
{
	if (BpPath.IsEmpty())
	{
		return nullptr;
	}

	if (BpPath.StartsWith(TEXT("level:"), ESearchCase::IgnoreCase))
	{
		const FString LevelPath = BpPath.Mid(6);
		if (LevelPath.Equals(TEXT("current"), ESearchCase::IgnoreCase))
		{
			if (!GEditor)
			{
				UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: GEditor is null for level:current"));
				return nullptr;
			}

			UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
			if (!EditorWorld || !EditorWorld->PersistentLevel)
			{
				UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: editor world or persistent level is null for level:current"));
				return nullptr;
			}

		UBlueprint* LevelBP = Cast<UBlueprint>(EditorWorld->PersistentLevel->GetLevelScriptBlueprint(true));
		if (!LevelBP)
		{
			UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: no level script blueprint found for level:current"));
		}
		return LevelBP;
	}

		const FString NormalizedPath = NormalizeObjectPath(LevelPath);
		UWorld* MapWorld = FindObject<UWorld>(nullptr, *NormalizedPath);
		if (!MapWorld || !MapWorld->PersistentLevel)
		{
			UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: level map not found in memory: %s"), *NormalizedPath);
			return nullptr;
		}

		UBlueprint* MapLevelBP = Cast<UBlueprint>(MapWorld->PersistentLevel->GetLevelScriptBlueprint(true));
		if (!MapLevelBP)
		{
			UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: no level script blueprint found for map: %s"), *NormalizedPath);
		}
		return MapLevelBP;
	}

	return LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(BpPath));
}

UEdGraph* FSmithUEBpAtomicAPI::FindGraph(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	// First pass: match by ObjectName (most common case)
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}
	// Second pass: match by FunctionEntry's FunctionReference name (handles cases where
	// ObjectName differs from the logical function name, e.g. after CloneGraph renaming)
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) { continue; }
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
			{
				FName FuncName = Entry->FunctionReference.GetMemberName();
				if (!FuncName.IsNone() && FuncName.ToString().Equals(GraphName, ESearchCase::IgnoreCase))
				{
					return Graph;
				}
				break; // Only one FunctionEntry per graph
			}
		}
	}
	return nullptr;
}

FString FSmithUEBpAtomicAPI::CreateNode(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeClass, FVector2D Position, const TSharedPtr<FJsonObject>& ExtraParams)
{
	if (!Blueprint || !Graph)
	{
		return FString();
	}
	UClass* NodeType = ResolveClassByName(NodeClass, UEdGraphNode::StaticClass(), TEXT('U'));
	// Fallback: if NodeClass contains "::" and resolution failed, treat as K2Node_CallFunction shorthand
	if (!NodeType && NodeClass.Contains(TEXT("::")))
	{
		NodeType = UK2Node_CallFunction::StaticClass();
		if (ExtraParams.IsValid() && !ExtraParams->HasField(TEXT("function_name")))
		{
			const_cast<FJsonObject&>(*ExtraParams).SetStringField(TEXT("function_name"), NodeClass);
		}
		else if (!ExtraParams.IsValid())
		{
			// Can't set function_name without ExtraParams - fail
			return FString();
		}
	}
	if (!NodeType)
	{
		return FString();
	}
	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, NodeType);
	if (!NewNode)
	{
		return FString();
	}
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(NewNode))
	{
		FString FunctionName;
		const bool bHasFunctionName =
			ExtraParams.IsValid() && ExtraParams->TryGetStringField(TEXT("function_name"), FunctionName) && !FunctionName.IsEmpty();

		if (bHasFunctionName)
		{
			UFunction* Function = FindFunctionByName(FunctionName);
			if (!Function)
			{
				return FString();
			}
			CallNode->SetFromFunction(Function);
		}
		// Self-configuring subclasses (K2Node_GetEditorProperty / SetEditorProperty,
		// and others that call SetExternalMember in their constructor) already know
		// their function -- requiring function_name would make them uncreatable.
		else if (CallNode->FunctionReference.GetMemberName().IsNone())
		{
			return FString();
		}
	}
	else if (UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(NewNode))
	{
		FString VariableName;
		if (!ExtraParams.IsValid() || !ExtraParams->TryGetStringField(TEXT("variable_name"), VariableName)) { return FString(); }
		// Optional: owner_class allows targeting a variable that belongs to a foreign class
		// (e.g. a property on a Cast result). When absent, falls back to SetSelfMember
		// so existing callers are unaffected.
		FString OwnerClassName;
		if (ExtraParams.IsValid() && ExtraParams->TryGetStringField(TEXT("owner_class"), OwnerClassName))
		{
			UClass* OwnerClass = ResolveClassByName(OwnerClassName, UObject::StaticClass(), TEXT('U'));
			if (!OwnerClass) { OwnerClass = ResolveClassByName(OwnerClassName, UObject::StaticClass(), TEXT('A')); }
			if (OwnerClass) { VariableGetNode->VariableReference.SetExternalMember(FName(*VariableName), OwnerClass); }
			else { VariableGetNode->VariableReference.SetSelfMember(FName(*VariableName)); }
		}
		else { VariableGetNode->VariableReference.SetSelfMember(FName(*VariableName)); }
	}
	else if (UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(NewNode))
	{
		FString VariableName;
		if (!ExtraParams.IsValid() || !ExtraParams->TryGetStringField(TEXT("variable_name"), VariableName)) { return FString(); }
		// Optional: owner_class allows targeting a variable that belongs to a foreign class.
		FString OwnerClassName;
		if (ExtraParams.IsValid() && ExtraParams->TryGetStringField(TEXT("owner_class"), OwnerClassName))
		{
			UClass* OwnerClass = ResolveClassByName(OwnerClassName, UObject::StaticClass(), TEXT('U'));
			if (!OwnerClass) { OwnerClass = ResolveClassByName(OwnerClassName, UObject::StaticClass(), TEXT('A')); }
			if (OwnerClass) { VariableSetNode->VariableReference.SetExternalMember(FName(*VariableName), OwnerClass); }
			else { VariableSetNode->VariableReference.SetSelfMember(FName(*VariableName)); }
		}
		else { VariableSetNode->VariableReference.SetSelfMember(FName(*VariableName)); }
	}
	else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(NewNode))
	{
		FString MacroPath;
		if (!ExtraParams.IsValid() || !ExtraParams->TryGetStringField(TEXT("macro_path"), MacroPath)) { return FString(); }
		if (UEdGraph* MacroGraph = ResolveMacroGraph(MacroPath)) { MacroNode->SetMacroGraph(MacroGraph); } else { return FString(); }
	}
	else if (UK2Node_InputKey* InputKeyNode = Cast<UK2Node_InputKey>(NewNode))
	{
		FString KeyName;
		if (ExtraParams.IsValid() && ExtraParams->TryGetStringField(TEXT("key"), KeyName))
		{
			InputKeyNode->InputKey = FKey(*KeyName);
		}
	}
	else if (UK2Node_EnhancedInputAction* EIANode = Cast<UK2Node_EnhancedInputAction>(NewNode))
	{
		FString InputActionPath;
		if (ExtraParams.IsValid() && ExtraParams->TryGetStringField(TEXT("input_action"), InputActionPath))
		{
			UInputAction* Action = LoadObject<UInputAction>(nullptr, *NormalizeObjectPath(InputActionPath));
			if (Action) { EIANode->InputAction = Action; }
		}
	}
	else if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(NewNode))
	{
		FString TargetClassName;
		if (ExtraParams.IsValid() && ExtraParams->TryGetStringField(TEXT("target_class"), TargetClassName))
		{
			UClass* TargetClass = ResolveClassByName(TargetClassName, UObject::StaticClass(), TEXT('U'));
			if (!TargetClass) { TargetClass = ResolveClassByName(TargetClassName, UObject::StaticClass(), TEXT('A')); }
			if (TargetClass) { CastNode->TargetType = TargetClass; }
		}
	}
	Graph->Modify();
	NewNode->SetFlags(RF_Transactional);
	NewNode->CreateNewGuid();
	// Auto-place when no explicit position was requested. GetPositionFromJson()
	// returns (0,0) whenever the caller omits "position", which used to pile every
	// atomic/batch-created node at the origin (hard to review). When the requested
	// position is the origin, cascade the node to the right of the existing graph's
	// bounding box so nodes don't overlap. Explicit non-origin positions are honored
	// verbatim; run auto_layout_graph afterwards for full connection-aware layout.
	if (Position.IsNearlyZero())
	{
		Position = SmithUEBpAtomicAPIHelpers::ComputeCascadeNodePosition(Graph);
	}
	NewNode->NodePosX = FMath::RoundToInt(Position.X);
	NewNode->NodePosY = FMath::RoundToInt(Position.Y);
	Graph->AddNode(NewNode, false, false);
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();
	NewNode->ReconstructNode();
	// Mark as modified (non-structural) — the caller is responsible for a single
	// CompileBlueprint at the end. Structural marking here would trigger a full
	// skeleton recompile + actor reinstancing for every single node creation,
	// corrupting FTickFunction vtables on live actors.
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return NewNode->NodeGuid.ToString();
}

bool FSmithUEBpAtomicAPI::ConnectPins(UBlueprint* Blueprint, UEdGraph* Graph, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName)
{
	FString Error;
	return TryConnectPins(Blueprint, Graph, FString(), SourceNodeId, SourcePinName, TargetNodeId, TargetPinName, Error);
}

bool FSmithUEBpAtomicAPI::DisconnectPins(UBlueprint* Blueprint, UEdGraph* Graph, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName)
{
	using namespace SmithUEBpAtomicAPIHelpers;
	const FString GraphPath = FString();
	FString Error;
	return DisconnectPinsImpl(Blueprint, Graph, GraphPath, SourceNodeId, SourcePinName, TargetNodeId, TargetPinName, Error);
}

bool FSmithUEBpAtomicAPI::SetPinDefault(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeId, const FString& PinName, const FString& Value)
{
	FString Error;
	return TrySetPinDefault(Blueprint, Graph, FString(), NodeId, PinName, Value, Error);
}

bool FSmithUEBpAtomicAPI::CompileBlueprint(UBlueprint* Blueprint, TArray<FString>& OutErrors, bool bSkipGarbageCollection)
{
	OutErrors.Reset();
	if (!Blueprint)
	{
		OutErrors.Add(TEXT("Blueprint is null"));
		return false;
	}
	// 编译时始终跳过 GC, 防止其他蓝图 SCS 引用的组件模板被提前销毁
	// (例如蓝图组件类在重编译时会触发 FTickFunction 纯虚函数崩溃)
	// Always skip GC during compile to prevent premature destruction of templates
	// referenced by other Blueprints' SCS (e.g., Blueprint component classes).
	EBlueprintCompileOptions CompileFlags = EBlueprintCompileOptions::SkipGarbageCollection;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileFlags);
	if (Blueprint->Status == BS_Error)
	{
		// Collect actual error messages from graph nodes
		auto CollectNodeErrors = [&OutErrors](const TArray<TObjectPtr<UEdGraph>>& Graphs)
		{
			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph) continue;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node && Node->bHasCompilerMessage && Node->ErrorType <= (int32)EMessageSeverity::Error)
					{
						OutErrors.Add(FString::Printf(TEXT("[%s] %s"),
							*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
							*Node->ErrorMsg));
					}
				}
			}
		};
		CollectNodeErrors(Blueprint->UbergraphPages);
		CollectNodeErrors(Blueprint->FunctionGraphs);
		if (OutErrors.Num() == 0)
		{
			OutErrors.Add(TEXT("Blueprint compilation failed (no specific error messages available)"));
		}
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpCreate(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("name"), TEXT("parent_class"), TEXT("save_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString Name; FString ParentClassName; FString SavePath;
	Params->TryGetStringField(TEXT("name"), Name); Params->TryGetStringField(TEXT("parent_class"), ParentClassName); Params->TryGetStringField(TEXT("save_path"), SavePath);
	UClass* ParentClass = ResolveClassByName(ParentClassName, UObject::StaticClass(), TEXT('A'));
	if (!ParentClass) { ParentClass = ResolveClassByName(ParentClassName, UObject::StaticClass(), TEXT('U')); }
	// 回退: 如果 parent_class 看起来像蓝图路径 (含 "/"), 加载蓝图并使用其 GeneratedClass
	// Fallback: if parent_class looks like a Blueprint path, load the Blueprint and use its GeneratedClass
	if (!ParentClass && ParentClassName.Contains(TEXT("/")))
	{
		FString BpPathToLoad = ParentClassName;
		// Strip _C suffix if user passed the generated class path (e.g. /Game/BP_BoxPawn.BP_BoxPawn_C)
		if (BpPathToLoad.EndsWith(TEXT("_C")))
		{
			// Convert "/Game/BP_BoxPawn.BP_BoxPawn_C" to "/Game/BP_BoxPawn"
			int32 DotIdx;
			if (BpPathToLoad.FindLastChar(TEXT('.'), DotIdx))
			{
				BpPathToLoad = BpPathToLoad.Left(DotIdx);
			}
		}
		if (UBlueprint* ParentBP = LoadBlueprint(BpPathToLoad))
		{
			ParentClass = ParentBP->GeneratedClass;
		}
	}
	if (!ParentClass) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent class not found: %s"), *ParentClassName)); }
	const FString CleanSavePath = SavePath.EndsWith(TEXT("/")) ? SavePath.LeftChop(1) : SavePath;
	const FString PackagePath = FString::Printf(TEXT("%s/%s"), *CleanSavePath, *Name);
	if (LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(PackagePath))) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint already exists at target path")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpCreate", "SmithUE: Create Blueprint"));
	UPackage* Package = CreatePackage(*PackagePath);
	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!NewBlueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create Blueprint")); }
	FAssetRegistryModule::AssetCreated(NewBlueprint);
	NewBlueprint->MarkPackageDirty();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), PackagePath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpCreateInterface(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("name"), TEXT("save_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString Name, SavePath;
	Params->TryGetStringField(TEXT("name"), Name); Params->TryGetStringField(TEXT("save_path"), SavePath);
	const FString CleanSavePath = SavePath.EndsWith(TEXT("/")) ? SavePath.LeftChop(1) : SavePath;
	const FString PackagePath = FString::Printf(TEXT("%s/%s"), *CleanSavePath, *Name);
	if (LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(PackagePath))) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint already exists at target path")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpCreateInterface", "SmithUE: Create Blueprint Interface"));
	UPackage* Package = CreatePackage(*PackagePath);
	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(UInterface::StaticClass(), Package, FName(*Name), BPTYPE_Interface, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!NewBP) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create Blueprint Interface")); }
	FAssetRegistryModule::AssetCreated(NewBP);
	NewBP->MarkPackageDirty();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), PackagePath);
	Data->SetStringField(TEXT("type"), TEXT("BlueprintInterface"));
	Data->SetStringField(TEXT("hint"), TEXT("Add interface functions with bp_add_function; implement it on a Blueprint with bp_implement_interface."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpImplementInterface(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("interface_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath, InterfacePath;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("interface_path"), InterfacePath);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	// Resolve the interface class: a Blueprint-interface path (use its GeneratedClass) or a native UInterface class name.
	UClass* InterfaceClass = nullptr;
	if (InterfacePath.Contains(TEXT("/")))
	{
		FString CleanPath = InterfacePath;
		if (CleanPath.EndsWith(TEXT("_C"))) { int32 Dot; if (CleanPath.FindLastChar(TEXT('.'), Dot)) { CleanPath = CleanPath.Left(Dot); } }
		if (UBlueprint* IfaceBP = LoadBlueprint(CleanPath)) { InterfaceClass = IfaceBP->GeneratedClass; }
	}
	if (!InterfaceClass) { InterfaceClass = ResolveClassByName(InterfacePath, UInterface::StaticClass(), TEXT('U')); }
	if (!InterfaceClass) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Interface class not found: '%s' (pass a Blueprint-interface asset path or a native UInterface class name)"), *InterfacePath)); }

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpImplementInterface", "SmithUE: Implement Interface"));
	const bool bImplemented = FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClass->GetClassPathName());
	if (!bImplemented) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to implement interface '%s' (already implemented?)"), *InterfaceClass->GetName())); }
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	Blueprint->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), BpPath);
	Data->SetStringField(TEXT("interface"), InterfaceClass->GetName());
	Data->SetBoolField(TEXT("implemented"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("function_name") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString FunctionName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("function_name"), FunctionName);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	if (FindGraph(Blueprint, FunctionName)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph already exists")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddFunction", "SmithUE: Add Blueprint Function"));
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, NewGraph, true, (UFunction*)nullptr);
	UK2Node_FunctionEntry* EntryNode = nullptr; UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes) { EntryNode = EntryNode ? EntryNode : Cast<UK2Node_FunctionEntry>(Node); ResultNode = ResultNode ? ResultNode : Cast<UK2Node_FunctionResult>(Node); }
	const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr; const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
	Params->TryGetArrayField(TEXT("inputs"), Inputs); Params->TryGetArrayField(TEXT("outputs"), Outputs);
	if (Inputs && !EntryNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Function entry node not found")); }
	// UE 5.x: void functions may not auto-create FunctionResult node. Create one if outputs are requested.
	if (Outputs && Outputs->Num() > 0 && !ResultNode)
	{
		FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*NewGraph);
		ResultNode = ResultCreator.CreateNode();
		if (!ResultNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create function result node")); }
		ResultNode->NodePosX = 800;
		ResultNode->NodePosY = 0;
		ResultCreator.Finalize();
		ResultNode->AllocateDefaultPins();
	}
	if (Inputs)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Inputs)
		{
			const TSharedPtr<FJsonObject> PinObject = Value.IsValid() ? Value->AsObject() : nullptr;
			FString PinName; FString PinTypeName; FEdGraphPinType PinType;
			if (!GetNamedTypeField(PinObject, PinName, PinTypeName) || !ResolvePinType(PinTypeName, PinType)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid function input definition")); }
			EntryNode->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Output);
		}
	}
	if (Outputs)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Outputs)
		{
			const TSharedPtr<FJsonObject> PinObject = Value.IsValid() ? Value->AsObject() : nullptr;
			FString PinName; FString PinTypeName; FEdGraphPinType PinType;
			if (!GetNamedTypeField(PinObject, PinName, PinTypeName) || !ResolvePinType(PinTypeName, PinType)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid function output definition")); }
			ResultNode->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Input);
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("graph_name"), FunctionName);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpCreateNode(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_class") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString GraphName; FString NodeClass;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("graph_name"), GraphName); Params->TryGetStringField(TEXT("node_class"), NodeClass);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph not found")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpCreateNode", "SmithUE: Create Blueprint Node"));
	const FString NodeId = CreateNode(Blueprint, Graph, NodeClass, GetPositionFromJson(Params), Params);
	if (NodeId.IsEmpty()) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create node")); }
	FSmithUEToolRegistry::Get().NidSession.MarkStale(BpPath + TEXT("::") + GraphName);
	TSharedPtr<FJsonObject> Response = MakeNodeResponse(NodeId);
	Response->SetBoolField(TEXT("nid_stale"), true);
	return Response;
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpConnectPins(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("source_node_id"), TEXT("source_pin"), TEXT("target_node_id"), TEXT("target_pin") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString GraphName; FString SourceNodeId; FString SourcePin; FString TargetNodeId; FString TargetPin;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("graph_name"), GraphName); Params->TryGetStringField(TEXT("source_node_id"), SourceNodeId); Params->TryGetStringField(TEXT("source_pin"), SourcePin); Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId); Params->TryGetStringField(TEXT("target_pin"), TargetPin);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph not found")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpConnectPins", "SmithUE: Connect Blueprint Pins"));
	const FString GraphPath = BpPath + TEXT("::") + GraphName;
	if (!TryConnectPins(Blueprint, Graph, GraphPath, SourceNodeId, SourcePin, TargetNodeId, TargetPin, Error))
	{
		if (TSharedPtr<FJsonObject> StructuredError = FSmithUECommonUtils::ParseJson(Error); StructuredError.IsValid())
		{
			return StructuredError;
		}
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("connected"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpDisconnectPins(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("source_node_id"), TEXT("source_pin"), TEXT("target_node_id"), TEXT("target_pin") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString GraphName; FString SourceNodeId; FString SourcePin; FString TargetNodeId; FString TargetPin;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("graph_name"), GraphName); Params->TryGetStringField(TEXT("source_node_id"), SourceNodeId); Params->TryGetStringField(TEXT("source_pin"), SourcePin); Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId); Params->TryGetStringField(TEXT("target_pin"), TargetPin);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph not found")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpDisconnectPins", "SmithUE: Disconnect Blueprint Pins"));
	const FString GraphPath = BpPath + TEXT("::") + GraphName;
	if (!DisconnectPinsImpl(Blueprint, Graph, GraphPath, SourceNodeId, SourcePin, TargetNodeId, TargetPin, Error))
	{
		if (TSharedPtr<FJsonObject> StructuredError = FSmithUECommonUtils::ParseJson(Error); StructuredError.IsValid())
		{
			return StructuredError;
		}
		return FSmithUECommonUtils::CreateErrorResponse(Error.IsEmpty() ? TEXT("Failed to disconnect pins") : Error);
	}
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("disconnected"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpSetPinDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_id"), TEXT("pin_name"), TEXT("value") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString GraphName; FString NodeId; FString PinName; FString Value;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("graph_name"), GraphName); Params->TryGetStringField(TEXT("node_id"), NodeId); Params->TryGetStringField(TEXT("pin_name"), PinName); Params->TryGetStringField(TEXT("value"), Value);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph not found")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpSetPinDefault", "SmithUE: Set Blueprint Pin Default"));
	const FString GraphPath = BpPath + TEXT("::") + GraphName;
	if (!TrySetPinDefault(Blueprint, Graph, GraphPath, NodeId, PinName, Value, Error))
	{
		if (TSharedPtr<FJsonObject> StructuredError = FSmithUECommonUtils::ParseJson(Error); StructuredError.IsValid())
		{
			return StructuredError;
		}
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("updated"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpSetAnimNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_id"), TEXT("property"), TEXT("value") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FString BpPath, GraphName, NodeId, PropertyName, Value;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	Params->TryGetStringField(TEXT("node_id"), NodeId);
	Params->TryGetStringField(TEXT("property"), PropertyName);
	Params->TryGetStringField(TEXT("value"), Value);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	UAnimGraphNode_Base* AnimNode = ResolveAnimGraphNode(Blueprint, Graph, BpPath, GraphName, NodeId, Error);
	if (!AnimNode) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FStructProperty* NodeStructProperty = FindAnimNodeStructProperty(AnimNode);
	if (!NodeStructProperty)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Anim node '%s' has no internal FAnimNode struct property (usually named 'Node'); cannot set '%s'."), *NodeId, *PropertyName));
	}

	FProperty* InnerProperty = FindAnimNodeInnerProperty(NodeStructProperty, PropertyName);
	if (!InnerProperty)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Anim node property '%s' not found on %s. Available properties: %s. Run bp_read_anim_node for details."), *PropertyName, *NodeStructProperty->Struct->GetName(), *JoinPropertyNames(GetAnimNodeInnerPropertyNames(NodeStructProperty))));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpSetAnimNodeProperty", "SmithUE: Set Anim Node Property"));
	Blueprint->Modify();
	AnimNode->Modify();
	uint8* NodeStructPtr = NodeStructProperty->ContainerPtrToValuePtr<uint8>(AnimNode);
	void* ValuePtr = InnerProperty->ContainerPtrToValuePtr<void>(NodeStructPtr);
	FString BeforeValue;
	InnerProperty->ExportTextItem_Direct(BeforeValue, ValuePtr, nullptr, AnimNode, PPF_None);
	FOutputDeviceNull ErrorDevice;
	const TCHAR* ImportResult = InnerProperty->ImportText_Direct(*Value, ValuePtr, AnimNode, PPF_None, &ErrorDevice);
	if (!ImportResult)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to import value '%s' for anim node property '%s' (%s). Use Unreal ImportText format; asset refs often need Class'/Game/Path.Asset'."), *Value, *InnerProperty->GetName(), *InnerProperty->GetClass()->GetName()));
	}

	FString AfterValue;
	InnerProperty->ExportTextItem_Direct(AfterValue, ValuePtr, nullptr, AnimNode, PPF_None);
	AnimNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("updated"), true);
	Data->SetStringField(TEXT("property"), InnerProperty->GetName());
	Data->SetStringField(TEXT("before"), BeforeValue);
	Data->SetStringField(TEXT("after"), AfterValue);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpExposeAnimPin(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_id"), TEXT("property"), TEXT("show") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FString BpPath, GraphName, NodeId, PropertyName;
	bool bShow = false;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	Params->TryGetStringField(TEXT("node_id"), NodeId);
	Params->TryGetStringField(TEXT("property"), PropertyName);
	Params->TryGetBoolField(TEXT("show"), bShow);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	UAnimGraphNode_Base* AnimNode = ResolveAnimGraphNode(Blueprint, Graph, BpPath, GraphName, NodeId, Error);
	if (!AnimNode) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	const int32 OptionalPinIndex = FindOptionalAnimPinIndex(AnimNode, PropertyName);
	if (OptionalPinIndex == INDEX_NONE)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Optional anim pin property '%s' not found. Available optional pins: %s. Run bp_read_anim_node for details."), *PropertyName, *JoinPropertyNames(GetOptionalAnimPinNames(AnimNode))));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpExposeAnimPin", "SmithUE: Expose Anim Pin"));
	Blueprint->Modify();
	AnimNode->Modify();
	const bool bBefore = AnimNode->ShowPinForProperties[OptionalPinIndex].bShowPin;
	AnimNode->SetPinVisibility(bShow, OptionalPinIndex);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FSmithUEToolRegistry::Get().NidSession.MarkStale(BpPath + TEXT("::") + GraphName);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("property"), AnimNode->ShowPinForProperties[OptionalPinIndex].PropertyName.ToString());
	Data->SetBoolField(TEXT("before_show"), bBefore);
	Data->SetBoolField(TEXT("show"), AnimNode->ShowPinForProperties[OptionalPinIndex].bShowPin);
	Data->SetBoolField(TEXT("nid_stale"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpBindAnimProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_id"), TEXT("property"), TEXT("variable") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FString BpPath, GraphName, NodeId, PropertyName, VariableName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	Params->TryGetStringField(TEXT("node_id"), NodeId);
	Params->TryGetStringField(TEXT("property"), PropertyName);
	Params->TryGetStringField(TEXT("variable"), VariableName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	UAnimGraphNode_Base* AnimNode = ResolveAnimGraphNode(Blueprint, Graph, BpPath, GraphName, NodeId, Error);
	if (!AnimNode) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FStructProperty* NodeStructProperty = FindAnimNodeStructProperty(AnimNode);
	FProperty* InnerProperty = FindAnimNodeInnerProperty(NodeStructProperty, PropertyName);
	if (!InnerProperty)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Cannot bind unknown anim node property '%s'. Available properties: %s. Run bp_read_anim_node first."), *PropertyName, *JoinPropertyNames(GetAnimNodeInnerPropertyNames(NodeStructProperty))));
	}

	const int32 OptionalPinIndex = FindOptionalAnimPinIndex(AnimNode, InnerProperty->GetName());
	if (OptionalPinIndex == INDEX_NONE)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Anim node property '%s' is not bindable/exposable on this node. Bindable optional pins: %s."), *InnerProperty->GetName(), *JoinPropertyNames(GetOptionalAnimPinNames(AnimNode))));
	}

	const FName BindingName = AnimNode->ShowPinForProperties[OptionalPinIndex].PropertyName;
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpBindAnimProperty", "SmithUE: Bind Anim Property"));
	Blueprint->Modify();
	AnimNode->Modify();

	if (VariableName.IsEmpty())
	{
		TMap<FName, FAnimGraphNodePropertyBinding>* Bindings = GetAnimNodePropertyBindings(AnimNode);
		const bool bRemoved = Bindings ? (Bindings->Remove(BindingName) > 0) : false;
		AnimNode->ReconstructNode();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("property"), BindingName.ToString());
		Data->SetBoolField(TEXT("bound"), false);
		Data->SetBoolField(TEXT("removed"), bRemoved);
		return FSmithUECommonUtils::CreateSuccessResponse(Data);
	}

	if (!BlueprintHasMemberVariable(Blueprint, VariableName))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Member variable '%s' not found on this AnimBlueprint. bp_bind_anim_property only supports member variables (not wires/functions/external paths). Available member variables: %s."), *VariableName, *JoinPropertyNames(GetBlueprintMemberVariableNames(Blueprint))));
	}

	AnimNode->SetPinVisibility(true, OptionalPinIndex);
	if (UEdGraphPin* Pin = AnimNode->FindPin(BindingName))
	{
		Pin->BreakAllPinLinks();
	}

	FAnimGraphNodePropertyBinding Binding;
	Binding.PropertyName = BindingName;
	Binding.PropertyPath.Add(VariableName);
	Binding.PathAsText = FText::FromString(VariableName);
	Binding.Type = EAnimGraphNodePropertyBindingType::Property;
	Binding.bIsBound = true;
	TMap<FName, FAnimGraphNodePropertyBinding>* Bindings = GetAnimNodePropertyBindings(AnimNode);
	if (!Bindings)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("This anim node has no binding container (GetMutableBinding() is null). Reopen the AnimBlueprint or reconstruct the node, then retry bp_bind_anim_property."));
	}
	Bindings->Add(BindingName, Binding);
	AnimNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("property"), BindingName.ToString());
	Data->SetStringField(TEXT("variable"), VariableName);
	Data->SetBoolField(TEXT("bound"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpReadAnimNode(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_id") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FString BpPath, GraphName, NodeId;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	Params->TryGetStringField(TEXT("node_id"), NodeId);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	UAnimGraphNode_Base* AnimNode = ResolveAnimGraphNode(Blueprint, Graph, BpPath, GraphName, NodeId, Error);
	if (!AnimNode) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FStructProperty* NodeStructProperty = FindAnimNodeStructProperty(AnimNode);
	if (!NodeStructProperty)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Anim node '%s' has no internal FAnimNode struct property to inspect."), *NodeId));
	}

	uint8* NodeStructPtr = NodeStructProperty->ContainerPtrToValuePtr<uint8>(AnimNode);
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;
	for (TFieldIterator<FProperty> It(NodeStructProperty->Struct); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property) { continue; }
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(NodeStructPtr);
		FString Value;
		Property->ExportTextItem_Direct(Value, ValuePtr, nullptr, AnimNode, PPF_None);

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Property->GetName());
		PropObj->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
		PropObj->SetStringField(TEXT("value"), Value);
		PropObj->SetBoolField(TEXT("has_optional_pin"), FindOptionalAnimPinIndex(AnimNode, Property->GetName()) != INDEX_NONE);
		PropertiesArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (const FOptionalPinFromProperty& OptionalPin : AnimNode->ShowPinForProperties)
	{
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("property"), OptionalPin.PropertyName.ToString());
		PinObj->SetStringField(TEXT("friendly_name"), OptionalPin.PropertyFriendlyName);
		PinObj->SetBoolField(TEXT("show"), OptionalPin.bShowPin);
		PinObj->SetBoolField(TEXT("can_toggle_visibility"), OptionalPin.bCanToggleVisibility);
		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}

	TArray<TSharedPtr<FJsonValue>> BindingsArray;
	static const TMap<FName, FAnimGraphNodePropertyBinding> EmptyAnimBindings;
	const TMap<FName, FAnimGraphNodePropertyBinding>* NodeBindings = GetAnimNodePropertyBindings(AnimNode);
	for (const TPair<FName, FAnimGraphNodePropertyBinding>& BindingPair : (NodeBindings ? *NodeBindings : EmptyAnimBindings))
	{
		TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
		BindingObj->SetStringField(TEXT("property"), BindingPair.Key.ToString());
		AppendJsonNameArray(BindingObj, TEXT("property_path"), BindingPair.Value.PropertyPath);
		BindingObj->SetStringField(TEXT("path_text"), BindingPair.Value.PathAsText.ToString());
		BindingObj->SetBoolField(TEXT("is_bound"), BindingPair.Value.bIsBound);
		BindingObj->SetStringField(TEXT("type"), BindingPair.Value.Type == EAnimGraphNodePropertyBindingType::Function ? TEXT("Function") : (BindingPair.Value.Type == EAnimGraphNodePropertyBindingType::Property ? TEXT("Property") : TEXT("None")));
		BindingsArray.Add(MakeShared<FJsonValueObject>(BindingObj));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), BpPath);
	Data->SetStringField(TEXT("graph_name"), GraphName);
	Data->SetStringField(TEXT("node_id"), AnimNode->NodeGuid.ToString());
	Data->SetStringField(TEXT("node_class"), AnimNode->GetClass()->GetName());
	Data->SetStringField(TEXT("node_struct_property"), NodeStructProperty->GetName());
	Data->SetStringField(TEXT("node_struct_type"), NodeStructProperty->Struct->GetName());
	Data->SetArrayField(TEXT("settable_properties"), PropertiesArray);
	Data->SetArrayField(TEXT("optional_pins"), PinsArray);
	Data->SetArrayField(TEXT("bindings"), BindingsArray);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddStateMachine(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FString BpPath, GraphName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Cast<UAnimBlueprint>(Blueprint)) { return CreateMissingAnimBlueprintError(BpPath); }

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("AnimGraph '%s' not found. bp_add_state_machine only creates UAnimGraphNode_StateMachine inside an existing AnimBlueprint AnimGraph; use anim_read_blueprint/bp_describe_graph to find the graph name."), *GraphName));
	}
	if (!IsAnimationGraph(Graph))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' is '%s', not an AnimGraph/UAnimationGraph. State machines can only be added to AnimBlueprint AnimGraph graphs."), *GraphName, *Graph->GetClass()->GetName()));
	}

	const FVector2D Position = GetPositionFromJson(Params);
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddStateMachine", "SmithUE: Add Anim State Machine"));
	Blueprint->Modify();
	Graph->Modify();

	UAnimGraphNode_StateMachine* StateMachineNode = NewObject<UAnimGraphNode_StateMachine>(Graph, UAnimGraphNode_StateMachine::StaticClass());
	if (!StateMachineNode)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to allocate UAnimGraphNode_StateMachine"));
	}

	StateMachineNode->SetFlags(RF_Transactional);
	StateMachineNode->CreateNewGuid();
	StateMachineNode->NodePosX = FMath::RoundToInt(Position.X);
	StateMachineNode->NodePosY = FMath::RoundToInt(Position.Y);
	Graph->AddNode(StateMachineNode, false, false);
	StateMachineNode->PostPlacedNewNode();
	StateMachineNode->AllocateDefaultPins();
	StateMachineNode->ReconstructNode();

	if (!StateMachineNode->EditorStateMachineGraph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("State-machine node was created but EditorStateMachineGraph is null. This should be created by UAnimGraphNode_StateMachineBase::PostPlacedNewNode; rebuild with AnimGraph module support."));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FSmithUEToolRegistry::Get().NidSession.MarkStale(GraphPathKey(BpPath, GraphName));

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("node_id"), StateMachineNode->NodeGuid.ToString());
	Data->SetStringField(TEXT("state_machine_graph"), StateMachineNode->EditorStateMachineGraph->GetName());
	Data->SetStringField(TEXT("entry_node_id"), StateMachineNode->EditorStateMachineGraph->EntryNode ? StateMachineNode->EditorStateMachineGraph->EntryNode->NodeGuid.ToString() : FString());
	Data->SetBoolField(TEXT("nid_stale"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddAnimState(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("state_machine"), TEXT("state_name") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FString BpPath, StateMachineRef, StateName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("state_machine"), StateMachineRef);
	Params->TryGetStringField(TEXT("state_name"), StateName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	UAnimationStateMachineGraph* StateMachineGraph = ResolveStateMachineGraph(Blueprint, BpPath, StateMachineRef, Error);
	if (!StateMachineGraph) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		if (UAnimStateNode* ExistingState = Cast<UAnimStateNode>(Node))
		{
			if (ExistingState->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
			{
				return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("State '%s' already exists in state machine '%s'. Use bp_read_state_machine to inspect existing state node_ids/bound_graphs."), *StateName, *StateMachineGraph->GetName()));
			}
		}
	}

	const bool bWasFirstState = !HasRealStates(StateMachineGraph);
	const FVector2D Position = GetPositionFromJson(Params);
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddAnimState", "SmithUE: Add Anim State"));
	Blueprint->Modify();
	StateMachineGraph->Modify();

	UAnimStateNode* StateNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(StateMachineGraph, NewObject<UAnimStateNode>(), Position, false);
	if (!StateNode || !StateNode->BoundGraph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UAnimStateNode with BoundGraph. Engine PostPlacedNewNode should create UAnimationStateGraph; check AnimGraph module dependency."));
	}

	FEdGraphUtilities::RenameGraphToNameOrCloseToName(StateNode->BoundGraph, StateName);
	const FString BoundGraphName = StateNode->BoundGraph->GetName();

	bool bEntryWired = false;
	if (bWasFirstState)
	{
		UAnimStateEntryNode* EntryNode = EnsureStateMachineEntry(StateMachineGraph);
		UEdGraphPin* EntryPin = FirstPinByDirection(EntryNode, EGPD_Output);
		UEdGraphPin* StateInputPin = StateNode->GetInputPin();
		if (EntryPin && StateInputPin && StateMachineGraph->GetSchema())
		{
			bEntryWired = StateMachineGraph->GetSchema()->TryCreateConnection(EntryPin, StateInputPin);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	MarkStateMachineGraphStale(BpPath, StateMachineGraph);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state_node_id"), StateNode->NodeGuid.ToString());
	Data->SetStringField(TEXT("state_name"), StateNode->GetStateName());
	Data->SetStringField(TEXT("bound_graph"), BoundGraphName);
	Data->SetStringField(TEXT("state_machine_graph"), StateMachineGraph->GetName());
	Data->SetBoolField(TEXT("entry_wired"), bEntryWired);
	Data->SetBoolField(TEXT("nid_stale"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddAnimTransition(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("state_machine"), TEXT("from_state"), TEXT("to_state") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FString BpPath, StateMachineRef, FromStateRef, ToStateRef;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("state_machine"), StateMachineRef);
	Params->TryGetStringField(TEXT("from_state"), FromStateRef);
	Params->TryGetStringField(TEXT("to_state"), ToStateRef);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	UAnimationStateMachineGraph* StateMachineGraph = ResolveStateMachineGraph(Blueprint, BpPath, StateMachineRef, Error);
	if (!StateMachineGraph) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	UAnimStateNode* FromStateNode = FindAnimStateNode(StateMachineGraph, BpPath, FromStateRef, Error);
	if (!FromStateNode) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	UAnimStateNode* ToStateNode = FindAnimStateNode(StateMachineGraph, BpPath, ToStateRef, Error);
	if (!ToStateNode) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	if (FromStateNode == ToStateNode)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("from_state and to_state resolve to the same state. bp_add_anim_transition requires two different UAnimStateNode states."));
	}

	const FVector2D Position = GetPositionFromJson(Params);
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddAnimTransition", "SmithUE: Add Anim Transition"));
	Blueprint->Modify();
	StateMachineGraph->Modify();

	UAnimStateTransitionNode* TransitionNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateTransitionNode>(StateMachineGraph, NewObject<UAnimStateTransitionNode>(), Position, false);
	if (!TransitionNode || !TransitionNode->GetBoundGraph())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UAnimStateTransitionNode with rule BoundGraph. Engine PostPlacedNewNode should create UAnimationTransitionGraph; check AnimGraph module dependency."));
	}
	TransitionNode->CreateConnections(FromStateNode, ToStateNode);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	MarkStateMachineGraphStale(BpPath, StateMachineGraph);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("transition_node_id"), TransitionNode->NodeGuid.ToString());
	Data->SetStringField(TEXT("from_state"), FromStateNode->GetStateName());
	Data->SetStringField(TEXT("to_state"), ToStateNode->GetStateName());
	Data->SetStringField(TEXT("rule_graph"), TransitionNode->GetBoundGraph()->GetName());
	Data->SetStringField(TEXT("state_machine_graph"), StateMachineGraph->GetName());
	Data->SetBoolField(TEXT("nid_stale"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpReadStateMachine(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("state_machine") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	FString BpPath, StateMachineRef;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("state_machine"), StateMachineRef);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	UAnimationStateMachineGraph* StateMachineGraph = ResolveStateMachineGraph(Blueprint, BpPath, StateMachineRef, Error);
	if (!StateMachineGraph) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

	TArray<TSharedPtr<FJsonValue>> StatesArray;
	TArray<TSharedPtr<FJsonValue>> TransitionsArray;
	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
		{
			TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
			StateObj->SetStringField(TEXT("node_id"), StateNode->NodeGuid.ToString());
			StateObj->SetStringField(TEXT("state_name"), StateNode->GetStateName());
			StateObj->SetStringField(TEXT("bound_graph"), StateNode->BoundGraph ? StateNode->BoundGraph->GetName() : FString());
			StatesArray.Add(MakeShared<FJsonValueObject>(StateObj));
		}
		else if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
		{
			UAnimStateNodeBase* PreviousState = TransitionNode->GetPreviousState();
			UAnimStateNodeBase* NextState = TransitionNode->GetNextState();
			TSharedPtr<FJsonObject> TransitionObj = MakeShared<FJsonObject>();
			TransitionObj->SetStringField(TEXT("node_id"), TransitionNode->NodeGuid.ToString());
			TransitionObj->SetStringField(TEXT("from_state"), PreviousState ? PreviousState->GetStateName() : FString());
			TransitionObj->SetStringField(TEXT("to_state"), NextState ? NextState->GetStateName() : FString());
			TransitionObj->SetStringField(TEXT("rule_graph"), TransitionNode->GetBoundGraph() ? TransitionNode->GetBoundGraph()->GetName() : FString());
			TransitionObj->SetBoolField(TEXT("bidirectional"), TransitionNode->Bidirectional);
			TransitionObj->SetNumberField(TEXT("priority_order"), TransitionNode->PriorityOrder);
			TransitionsArray.Add(MakeShared<FJsonValueObject>(TransitionObj));
		}
	}

	UAnimStateEntryNode* EntryNode = EnsureStateMachineEntry(StateMachineGraph);
	UEdGraphNode* EntryOutputNode = EntryNode ? EntryNode->GetOutputNode() : nullptr;
	UAnimStateNodeBase* EntryState = Cast<UAnimStateNodeBase>(EntryOutputNode);
	UAnimGraphNode_StateMachineBase* StateMachineNode = FindStateMachineNodeForGraph(Blueprint, StateMachineGraph);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), BpPath);
	Data->SetStringField(TEXT("state_machine_graph"), StateMachineGraph->GetName());
	Data->SetStringField(TEXT("state_machine_node_id"), StateMachineNode ? StateMachineNode->NodeGuid.ToString() : FString());
	Data->SetStringField(TEXT("entry_node_id"), EntryNode ? EntryNode->NodeGuid.ToString() : FString());
	Data->SetStringField(TEXT("entry_state"), EntryState ? EntryState->GetStateName() : FString());
	Data->SetArrayField(TEXT("states"), StatesArray);
	Data->SetArrayField(TEXT("transitions"), TransitionsArray);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpDeleteNode(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_id") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString GraphName; FString NodeId;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("graph_name"), GraphName); Params->TryGetStringField(TEXT("node_id"), NodeId);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph not found")); }
	FString ResolveError;
	UEdGraphNode* Node = ResolveNodeId(Graph, BpPath + TEXT("::") + GraphName, NodeId, ResolveError);
	if (!Node) { return FSmithUECommonUtils::CreateErrorResponse(ResolveError.IsEmpty() ? TEXT("Node not found") : ResolveError); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpDeleteNode", "SmithUE: Delete Blueprint Node"));
	Node->BreakAllNodeLinks();
	Graph->RemoveNode(Node);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FSmithUEToolRegistry::Get().NidSession.MarkStale(BpPath + TEXT("::") + GraphName);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("deleted"), true);
	Data->SetBoolField(TEXT("nid_stale"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("var_name"), TEXT("var_type") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString VarName; FString VarTypeName; FString DefaultValue; FString Category;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("var_name"), VarName); Params->TryGetStringField(TEXT("var_type"), VarTypeName); Params->TryGetStringField(TEXT("default_value"), DefaultValue); Params->TryGetStringField(TEXT("category"), Category);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	FEdGraphPinType PinType;
	if (!ResolvePinType(VarTypeName, PinType)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Unsupported variable type")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddVariable", "SmithUE: Add Blueprint Variable"));
	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VarName), PinType, DefaultValue)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to add variable")); }
	if (!Category.IsEmpty()) { FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(*VarName), nullptr, FText::FromString(Category)); }
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("var_name"), VarName);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_remove_variable
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRemoveVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("var_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, VarName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("var_name"), VarName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path"));
	}

	// Find the variable in NewVariables
	const FName VarFName(*VarName);
	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < Blueprint->NewVariables.Num(); ++i)
	{
		if (Blueprint->NewVariables[i].VarName == VarFName)
		{
			FoundIndex = i;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Variable not found: '%s'"), *VarName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRemoveVariable", "SmithUE: Remove Blueprint Variable"));
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarFName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("var_name"), VarName);
	Data->SetBoolField(TEXT("removed"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

static int32 FindBpVariableIndex(UBlueprint* Blueprint, const FName& VarName)
{
	for (int32 i = 0; i < Blueprint->NewVariables.Num(); ++i)
	{
		if (Blueprint->NewVariables[i].VarName == VarName) { return i; }
	}
	return INDEX_NONE;
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRenameVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("var_name"), TEXT("new_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString BpPath, VarName, NewName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("var_name"), VarName);
	Params->TryGetStringField(TEXT("new_name"), NewName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	const FName OldF(*VarName), NewF(*NewName);
	if (FindBpVariableIndex(Blueprint, OldF) == INDEX_NONE)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable not found: '%s'"), *VarName));
	}
	if (FindBpVariableIndex(Blueprint, NewF) != INDEX_NONE)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("A variable named '%s' already exists"), *NewName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRenameVariable", "SmithUE: Rename Blueprint Variable"));
	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, OldF, NewF);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("var_name"), NewName);
	Data->SetStringField(TEXT("old_name"), VarName);
	Data->SetBoolField(TEXT("renamed"), FindBpVariableIndex(Blueprint, NewF) != INDEX_NONE);
	Data->SetStringField(TEXT("note"), TEXT("Renamed + references fixed up + recompiled. Call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpSetVariableFlags(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("var_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString BpPath, VarName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("var_name"), VarName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	const FName VarF(*VarName);
	if (FindBpVariableIndex(Blueprint, VarF) == INDEX_NONE)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable not found: '%s'"), *VarName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpSetVariableFlags", "SmithUE: Set Blueprint Variable Flags"));
	TSharedPtr<FJsonObject> Applied = MakeShared<FJsonObject>();

	bool bBoolVal = false;
	if (Params->TryGetBoolField(TEXT("instance_editable"), bBoolVal))
	{
		// "Blueprint only editable" is the inverse of instance-editable.
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VarF, !bBoolVal);
		Applied->SetBoolField(TEXT("instance_editable"), bBoolVal);
	}
	if (Params->TryGetBoolField(TEXT("blueprint_read_only"), bBoolVal))
	{
		FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, VarF, bBoolVal);
		Applied->SetBoolField(TEXT("blueprint_read_only"), bBoolVal);
	}
	if (Params->TryGetBoolField(TEXT("expose_on_spawn"), bBoolVal))
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VarF, nullptr, FName(TEXT("ExposeOnSpawn")), bBoolVal ? TEXT("true") : TEXT("false"));
		Applied->SetBoolField(TEXT("expose_on_spawn"), bBoolVal);
	}
	FString StrVal;
	if (Params->TryGetStringField(TEXT("category"), StrVal))
	{
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, VarF, nullptr, FText::FromString(StrVal));
		Applied->SetStringField(TEXT("category"), StrVal);
	}
	if (Params->TryGetStringField(TEXT("tooltip"), StrVal))
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VarF, nullptr, FName(TEXT("tooltip")), StrVal);
		Applied->SetStringField(TEXT("tooltip"), StrVal);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("var_name"), VarName);
	Data->SetObjectField(TEXT("applied"), Applied);
	Data->SetStringField(TEXT("note"), TEXT("Flags applied + recompiled. Call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddEventDispatcher(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("dispatcher_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString BpPath, DispName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("dispatcher_name"), DispName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	const FName DispF(*DispName);

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddEventDispatcher", "SmithUE: Add Event Dispatcher"));
	if (!UBlueprintEditorLibrary::AddEventDispatcher(Blueprint, DispF))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to add event dispatcher '%s' (name may already be in use)"), *DispName));
	}

	// Optional typed signature parameters.
	TArray<TSharedPtr<FJsonValue>> ParamsOut;
	const TArray<TSharedPtr<FJsonValue>>* SigParams = nullptr;
	if (Params->TryGetArrayField(TEXT("params"), SigParams) && SigParams)
	{
		for (const TSharedPtr<FJsonValue>& V : *SigParams)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) { continue; }
			FString PName, PType;
			(*Obj)->TryGetStringField(TEXT("name"), PName);
			(*Obj)->TryGetStringField(TEXT("type"), PType);
			if (PName.IsEmpty() || PType.IsEmpty()) { continue; }
			FEdGraphPinType PinType;
			if (!ResolvePinType(PType, PinType))
			{
				UBlueprintEditorLibrary::RemoveEventDispatcher(Blueprint, DispF); // roll back — keep the op atomic
				return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported param type '%s' for '%s'"), *PType, *PName));
			}
			if (!UBlueprintEditorLibrary::AddEventDispatcherParameter(Blueprint, DispF, FName(*PName), PinType))
			{
				UBlueprintEditorLibrary::RemoveEventDispatcher(Blueprint, DispF); // roll back
				return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to add param '%s' to dispatcher '%s'"), *PName, *DispName));
			}
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), PName);
			P->SetStringField(TEXT("type"), PType);
			ParamsOut.Add(MakeShared<FJsonValueObject>(P));
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("dispatcher_name"), DispName);
	Data->SetNumberField(TEXT("param_count"), ParamsOut.Num());
	Data->SetArrayField(TEXT("params"), ParamsOut);
	Data->SetStringField(TEXT("note"), TEXT("Event dispatcher created + recompiled. Bind/Assign/Call it from graphs; call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRemoveEventDispatcher(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("dispatcher_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString BpPath, DispName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("dispatcher_name"), DispName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRemoveEventDispatcher", "SmithUE: Remove Event Dispatcher"));
	if (!UBlueprintEditorLibrary::RemoveEventDispatcher(Blueprint, FName(*DispName)))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Event dispatcher '%s' not found"), *DispName));
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("dispatcher_name"), DispName);
	Data->SetBoolField(TEXT("removed"), true);
	Data->SetStringField(TEXT("note"), TEXT("Event dispatcher removed + recompiled. Call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// Finds the function's entry/result terminator nodes within its graph.
static void FindFunctionTerminators(UEdGraph* Graph, UK2Node_FunctionEntry*& OutEntry, UK2Node_FunctionResult*& OutResult)
{
	OutEntry = nullptr; OutResult = nullptr;
	if (!Graph) { return; }
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!OutEntry) { OutEntry = Cast<UK2Node_FunctionEntry>(Node); }
		if (!OutResult) { OutResult = Cast<UK2Node_FunctionResult>(Node); }
	}
}

static bool NodeHasUserPin(UK2Node_EditablePinBase* Node, const FName& PinName)
{
	if (!Node) { return false; }
	for (const TSharedPtr<FUserPinInfo>& Pin : Node->UserDefinedPins)
	{
		if (Pin.IsValid() && Pin->PinName == PinName) { return true; }
	}
	return false;
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddFunctionParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("function_name"), TEXT("param_name"), TEXT("param_type") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString BpPath, FuncName, ParamName, ParamType, Direction;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("function_name"), FuncName);
	Params->TryGetStringField(TEXT("param_name"), ParamName);
	Params->TryGetStringField(TEXT("param_type"), ParamType);
	Params->TryGetStringField(TEXT("direction"), Direction);
	const bool bOutput = Direction.Equals(TEXT("output"), ESearchCase::IgnoreCase);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, FuncName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Function graph not found: '%s'"), *FuncName)); }

	FEdGraphPinType PinType;
	if (!ResolvePinType(ParamType, PinType)) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported param_type '%s'"), *ParamType)); }

	UK2Node_FunctionEntry* EntryNode = nullptr; UK2Node_FunctionResult* ResultNode = nullptr;
	FindFunctionTerminators(Graph, EntryNode, ResultNode);
	const FName ParamF(*ParamName);

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddFuncParam", "SmithUE: Add Function Parameter"));
	if (bOutput)
	{
		if (!ResultNode)
		{
			FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*Graph);
			ResultNode = ResultCreator.CreateNode();
			if (!ResultNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create function result node")); }
			ResultNode->NodePosX = 800; ResultNode->NodePosY = 0;
			ResultCreator.Finalize();
			ResultNode->AllocateDefaultPins();
		}
		if (NodeHasUserPin(ResultNode, ParamF)) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Output parameter '%s' already exists"), *ParamName)); }
		ResultNode->CreateUserDefinedPin(ParamF, PinType, EGPD_Input);
	}
	else
	{
		if (!EntryNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Function entry node not found")); }
		if (NodeHasUserPin(EntryNode, ParamF)) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Input parameter '%s' already exists"), *ParamName)); }
		EntryNode->CreateUserDefinedPin(ParamF, PinType, EGPD_Output);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("function_name"), FuncName);
	Data->SetStringField(TEXT("param_name"), ParamName);
	Data->SetStringField(TEXT("direction"), bOutput ? TEXT("output") : TEXT("input"));
	Data->SetStringField(TEXT("note"), TEXT("Parameter added + recompiled. Call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRemoveFunctionParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("function_name"), TEXT("param_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString BpPath, FuncName, ParamName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("function_name"), FuncName);
	Params->TryGetStringField(TEXT("param_name"), ParamName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, FuncName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Function graph not found: '%s'"), *FuncName)); }

	UK2Node_FunctionEntry* EntryNode = nullptr; UK2Node_FunctionResult* ResultNode = nullptr;
	FindFunctionTerminators(Graph, EntryNode, ResultNode);
	const FName ParamF(*ParamName);

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRemoveFuncParam", "SmithUE: Remove Function Parameter"));
	FString RemovedFrom;
	if (NodeHasUserPin(EntryNode, ParamF)) { EntryNode->RemoveUserDefinedPinByName(ParamF); RemovedFrom = TEXT("input"); }
	else if (NodeHasUserPin(ResultNode, ParamF)) { ResultNode->RemoveUserDefinedPinByName(ParamF); RemovedFrom = TEXT("output"); }
	else { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parameter '%s' not found on function '%s'"), *ParamName, *FuncName)); }
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("function_name"), FuncName);
	Data->SetStringField(TEXT("param_name"), ParamName);
	Data->SetStringField(TEXT("direction"), RemovedFrom);
	Data->SetBoolField(TEXT("removed"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddLocalVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("function_name"), TEXT("var_name"), TEXT("var_type") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString BpPath, FuncName, VarName, VarType, DefaultValue;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("function_name"), FuncName);
	Params->TryGetStringField(TEXT("var_name"), VarName);
	Params->TryGetStringField(TEXT("var_type"), VarType);
	Params->TryGetStringField(TEXT("default_value"), DefaultValue);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, FuncName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Function graph not found: '%s'"), *FuncName)); }

	FEdGraphPinType PinType;
	if (!ResolvePinType(VarType, PinType)) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported var_type '%s'"), *VarType)); }

	// AddLocalVariable does NOT check for duplicates — a second same-named local silently
	// breaks compilation. Guard explicitly.
	if (FBlueprintEditorUtils::FindLocalVariable(Blueprint, Graph, FName(*VarName)))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Local variable '%s' already exists in '%s'"), *VarName, *FuncName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddLocalVar", "SmithUE: Add Local Variable"));
	if (!FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, FName(*VarName), PinType, DefaultValue))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to add local variable '%s' (name may be in use)"), *VarName));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("function_name"), FuncName);
	Data->SetStringField(TEXT("var_name"), VarName);
	Data->SetStringField(TEXT("note"), TEXT("Local variable added. Call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddTimeline(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("timeline_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString BpPath, TimelineName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("timeline_name"), TimelineName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	const FName TimelineF(*TimelineName);
	if (Blueprint->FindTimelineTemplateByVariableName(TimelineF))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Timeline '%s' already exists"), *TimelineName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddTimeline", "SmithUE: Add Timeline"));
	UTimelineTemplate* Template = FBlueprintEditorUtils::AddNewTimeline(Blueprint, TimelineF);
	if (!Template)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to add timeline '%s'"), *TimelineName));
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("timeline_name"), TimelineName);
	Data->SetStringField(TEXT("note"), TEXT("Timeline created + recompiled. Add tracks and drive it via a K2Node_Timeline node (bp_create_node). Call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddComponent(const TSharedPtr<FJsonObject>& Params)
{
  FString Error;
  if (!Params->HasField(TEXT("component")) && Params->HasField(TEXT("component_name")))
      Params->SetStringField(TEXT("component"), Params->GetStringField(TEXT("component_name")));
  if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("component_class"), TEXT("component") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
  FString BpPath; FString ComponentClassName; FString ComponentName;
  Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("component_class"), ComponentClassName); if (!Params->TryGetStringField(TEXT("component"), ComponentName)) Params->TryGetStringField(TEXT("component_name"), ComponentName);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	if (!Blueprint->SimpleConstructionScript) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint has no SimpleConstructionScript")); }
	UClass* ComponentClass = ResolveClassByName(ComponentClassName, UActorComponent::StaticClass(), TEXT('U'));
	if (!ComponentClass) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Component class not found")); }

	// 如果组件类来自蓝图, 确保添加前先编译该蓝图 (防止未编译导致崩溃)
	// If this is a Blueprint-generated class, ensure its Blueprint is compiled first
	if (UBlueprint* ComponentBP = Cast<UBlueprint>(ComponentClass->ClassGeneratedBy))
	{
		if (ComponentBP->Status == BS_Dirty || ComponentBP->Status == BS_Unknown)
		{
			FKismetEditorUtilities::CompileBlueprint(ComponentBP, EBlueprintCompileOptions::SkipGarbageCollection);
			// 编译后重新获取生成类
			// Re-resolve the class after compilation
			ComponentClass = ComponentBP->GeneratedClass;
			if (!ComponentClass) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Component Blueprint failed to compile")); }
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddComponent", "SmithUE: Add Blueprint Component"));
	USCS_Node* SCSNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(*ComponentName));
	if (!SCSNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create component node")); }

	// 安全措施: 禁用组件模板的 Tick, 防止重编译时触发纯虚函数崩溃
	// Safety: disable tick on the component template to prevent pure virtual crashes
	// 但跳过 UMovementComponent 子类 — 它们依赖 Tick 驱动运动
	// Skip UMovementComponent subclasses — they require tick to function
	if (UActorComponent* Template = SCSNode->ComponentTemplate)
	{
		if (!Template->IsA<UMovementComponent>())
		{
			Template->PrimaryComponentTick.bCanEverTick = false;
			Template->PrimaryComponentTick.bStartWithTickEnabled = false;
		}
	}

	// 可选: 挂载到父组件实现层级结构
	// Optional: attach to parent component
	FString ParentName;
	if (Params->TryGetStringField(TEXT("parent"), ParentName) && !ParentName.IsEmpty())
	{
		USCS_Node* ParentNode = nullptr;
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(ParentName, ESearchCase::IgnoreCase))
			{
				ParentNode = Node;
				break;
			}
		}
		if (ParentNode) { ParentNode->AddChildNode(SCSNode); }
		else { Blueprint->SimpleConstructionScript->AddNode(SCSNode); }
	}
	else
	{
		Blueprint->SimpleConstructionScript->AddNode(SCSNode);
	}

	// Optional: set static mesh on StaticMeshComponent template
	FString StaticMeshPath;
	if (Params->TryGetStringField(TEXT("static_mesh"), StaticMeshPath) && !StaticMeshPath.IsEmpty())
	{
		if (UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(SCSNode->ComponentTemplate))
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);
			if (Mesh) { MeshComp->SetStaticMesh(Mesh); }
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
  Data->SetStringField(TEXT("component"), ComponentName);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_remove_component
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRemoveComponent(const TSharedPtr<FJsonObject>& Params)
{
  FString Error;
  if (!Params->HasField(TEXT("component")) && Params->HasField(TEXT("component_name")))
      Params->SetStringField(TEXT("component"), Params->GetStringField(TEXT("component_name")));
  if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("component") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
  FString BpPath; FString ComponentName;
  Params->TryGetStringField(TEXT("bp_path"), BpPath); if (!Params->TryGetStringField(TEXT("component"), ComponentName)) Params->TryGetStringField(TEXT("component_name"), ComponentName);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	if (!Blueprint->SimpleConstructionScript) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint has no SimpleConstructionScript")); }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: '%s'"), *ComponentName)); }

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRemoveComponent", "SmithUE: Remove Blueprint Component"));

	// 移除前将子节点重新挂载到目标的父节点 (防止子组件成为孤儿)
	// Reparent children to the target's parent before removing
	for (USCS_Node* Child : TargetNode->ChildNodes)
	{
		if (!Child) { continue; }
		// Find parent of target node
		USCS_Node* ParentOfTarget = nullptr;
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ChildNodes.Contains(TargetNode))
			{
				ParentOfTarget = Node;
				break;
			}
		}
		if (ParentOfTarget) { ParentOfTarget->AddChildNode(Child); }
		else { Blueprint->SimpleConstructionScript->AddNode(Child); }
	}
	TargetNode->ChildNodes.Empty();

	Blueprint->SimpleConstructionScript->RemoveNode(TargetNode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("removed"), true);
  Data->SetStringField(TEXT("component"), ComponentName);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRenameComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("component"), TEXT("new_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath;
	FString ComponentName;
	FString NewName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("component"), ComponentName);
	Params->TryGetStringField(TEXT("new_name"), NewName);
	NewName.TrimStartAndEndInline();
	if (NewName.IsEmpty())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("new_name cannot be empty"));
	}

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	if (!Blueprint->SimpleConstructionScript) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint has no SimpleConstructionScript")); }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (!Node) { continue; }
		const FString NodeName = Node->GetVariableName().ToString();
		if (NodeName.Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			continue;
		}
		if (NodeName.Equals(NewName, ESearchCase::IgnoreCase))
		{
			return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component name already exists: '%s'"), *NewName));
		}
	}
	if (!TargetNode)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: '%s'"), *ComponentName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRenameComponent", "SmithUE: Rename Blueprint Component"));
	Blueprint->Modify();
	const FName OldName = TargetNode->GetVariableName();
	FBlueprintEditorUtils::RenameComponentMemberVariable(Blueprint, TargetNode, FName(*NewName));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();

	TArray<FString> Errors;
	if (!FSmithUEBpAtomicAPI::CompileBlueprint(Blueprint, Errors, true))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint compilation failed after component rename: %s"), *FString::Join(Errors, TEXT("; "))));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("old_name"), OldName.ToString());
	Data->SetStringField(TEXT("new_name"), NewName);
	Data->SetStringField(TEXT("component_class"), TargetNode->ComponentClass ? TargetNode->ComponentClass->GetName() : TEXT(""));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_set_component_property
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpSetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
  FString Error;
  if (!Params->HasField(TEXT("component")) && Params->HasField(TEXT("component_name")))
      Params->SetStringField(TEXT("component"), Params->GetStringField(TEXT("component_name")));
  if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("component"), TEXT("property_name"), TEXT("value") }, Error))
  {
    return FSmithUECommonUtils::CreateErrorResponse(Error);
  }

  FString BpPath, ComponentName, PropertyName;
  Params->TryGetStringField(TEXT("bp_path"), BpPath);
  if (!Params->TryGetStringField(TEXT("component"), ComponentName))
      Params->TryGetStringField(TEXT("component_name"), ComponentName);
  Params->TryGetStringField(TEXT("property_name"), PropertyName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path"));
	}

	// --- Find component template ---
	UActorComponent* TargetComp = nullptr;

	// 1) Check SCS templates
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate)
			{
				FString NodeVarName = Node->GetVariableName().ToString();
				FString TemplateName = Node->ComponentTemplate->GetName();
				if (NodeVarName.Equals(ComponentName, ESearchCase::IgnoreCase) ||
					TemplateName.Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					TargetComp = Node->ComponentTemplate;
					break;
				}
			}
		}
	}

	// 2) Check CDO components (inherited from C++ parent)
	if (!TargetComp)
	{
		AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
		if (CDO)
		{
			for (UActorComponent* Comp : CDO->GetComponents())
			{
				if (Comp && Comp->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					TargetComp = Comp;
					break;
				}
			}
		}
	}

	if (!TargetComp)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Component not found: '%s'"), *ComponentName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpSetCompProp", "SmithUE: Set BP Component Property"));
	Blueprint->Modify();
	TargetComp->Modify();

	// --- Special handling: PostProcessMaterial ---
	if (PropertyName.Equals(TEXT("PostProcessMaterial"), ESearchCase::IgnoreCase))
	{
		FString MaterialPath;
		Params->TryGetStringField(TEXT("value"), MaterialPath);

		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
		if (!Material)
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Material not found: '%s'"), *MaterialPath));
		}

		// Works for both UCameraComponent and UPostProcessComponent
		FPostProcessSettings* PPSettings = nullptr;
		float* BlendWeightPtr = nullptr;

		if (UCameraComponent* Camera = Cast<UCameraComponent>(TargetComp))
		{
			PPSettings = &Camera->PostProcessSettings;
			BlendWeightPtr = &Camera->PostProcessBlendWeight;
		}
		else if (UPostProcessComponent* PPComp = Cast<UPostProcessComponent>(TargetComp))
		{
			PPSettings = &PPComp->Settings;
			BlendWeightPtr = &PPComp->BlendWeight;
		}

		if (!PPSettings)
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				TEXT("Component is not a CameraComponent or PostProcessComponent"));
		}

		FWeightedBlendable Entry;
		Entry.Weight = 1.0f;
		Entry.Object = Material;
		PPSettings->WeightedBlendables.Array.Add(Entry);

		if (BlendWeightPtr)
		{
			*BlendWeightPtr = 1.0f;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		Blueprint->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
  	Data->SetStringField(TEXT("component"), ComponentName);
		Data->SetStringField(TEXT("property_name"), TEXT("PostProcessMaterial"));
		Data->SetStringField(TEXT("material_path"), MaterialPath);
		Data->SetNumberField(TEXT("blendable_count"), PPSettings->WeightedBlendables.Array.Num());
		return FSmithUECommonUtils::CreateSuccessResponse(Data);
	}

	// --- Special handling: ChildActorClass on UChildActorComponent ---
	if (PropertyName.Equals(TEXT("ChildActorClass"), ESearchCase::IgnoreCase))
	{
		UChildActorComponent* ChildActorComp = Cast<UChildActorComponent>(TargetComp);
		if (!ChildActorComp)
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Component '%s' is not a ChildActorComponent"), *ComponentName));
		}

		FString ClassPath;
		Params->TryGetStringField(TEXT("value"), ClassPath);

		// Try loading as a Blueprint asset first (most common case for BP classes)
		UClass* ChildClass = nullptr;
		UBlueprint* ChildBP = LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(ClassPath));
		if (ChildBP)
		{
			// Ensure the child BP is compiled
			if (ChildBP->Status == BS_Dirty || ChildBP->Status == BS_Unknown)
			{
				FKismetEditorUtilities::CompileBlueprint(ChildBP, EBlueprintCompileOptions::SkipGarbageCollection);
			}
			ChildClass = ChildBP->GeneratedClass;
		}
		else
		{
			// Try loading as a native class or already-generated class path
			ChildClass = LoadObject<UClass>(nullptr, *NormalizeObjectPath(ClassPath));
			if (!ChildClass)
			{
				// Try appending _C suffix for Blueprint GeneratedClass paths
				FString GeneratedClassPath = ClassPath;
				if (!GeneratedClassPath.EndsWith(TEXT("_C")))
				{
					GeneratedClassPath += TEXT("_C");
				}
				ChildClass = LoadObject<UClass>(nullptr, *NormalizeObjectPath(GeneratedClassPath));
			}
		}

		if (!ChildClass)
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to resolve ChildActorClass: '%s'"), *ClassPath));
		}

		FString BeforeClass = ChildActorComp->GetChildActorClass() ? ChildActorComp->GetChildActorClass()->GetPathName() : TEXT("None");
		ChildActorComp->SetChildActorClass(ChildClass);
		FString AfterClass = ChildActorComp->GetChildActorClass() ? ChildActorComp->GetChildActorClass()->GetPathName() : TEXT("None");

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		Blueprint->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("component"), ComponentName);
		Data->SetStringField(TEXT("property_name"), TEXT("ChildActorClass"));
		Data->SetStringField(TEXT("before"), BeforeClass);
		Data->SetStringField(TEXT("after"), AfterClass);
		Data->SetBoolField(TEXT("changed"), BeforeClass != AfterClass);
		return FSmithUECommonUtils::CreateSuccessResponse(Data);
	}

	// --- General property setting ---
	FProperty* Prop = TargetComp->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property not found: '%s' on component '%s' (class %s)"),
				*PropertyName, *ComponentName, *TargetComp->GetClass()->GetName()));
	}

	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetComp);

	FString BeforeValue;
	Prop->ExportTextItem_Direct(BeforeValue, PropAddr, nullptr, TargetComp, PPF_None);

	TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("value"));
	FString ValueStr;
	if (JsonValue.IsValid())
	{
		if (JsonValue->Type == EJson::String) ValueStr = JsonValue->AsString();
		else if (JsonValue->Type == EJson::Number) ValueStr = FString::SanitizeFloat(JsonValue->AsNumber());
		else if (JsonValue->Type == EJson::Boolean) ValueStr = JsonValue->AsBool() ? TEXT("True") : TEXT("False");
	}

	bool bSuccess = false;
	if (!ValueStr.IsEmpty())
	{
		const TCHAR* Result = Prop->ImportText_Direct(*ValueStr, PropAddr, TargetComp, 0);
		bSuccess = (Result != nullptr);
	}

	if (!bSuccess && Prop->IsA<FBoolProperty>())
	{
		CastField<FBoolProperty>(Prop)->SetPropertyValue(PropAddr, JsonValue.IsValid() && JsonValue->AsBool());
		bSuccess = true;
	}

	if (!bSuccess)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to set property '%s' on component '%s'"),
				*PropertyName, *ComponentName));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();

	FString AfterValue;
	Prop->ExportTextItem_Direct(AfterValue, PropAddr, nullptr, TargetComp, PPF_None);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("component"), ComponentName);
	Data->SetStringField(TEXT("property_name"), PropertyName);
	Data->SetStringField(TEXT("before"), BeforeValue);
	Data->SetStringField(TEXT("after"), AfterValue);
	Data->SetBoolField(TEXT("changed"), BeforeValue != AfterValue);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_set_component_collision
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpSetComponentCollision(const TSharedPtr<FJsonObject>& Params)
{
	FString BpPath, FolderPath, Component, ObjectType;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("folder_path"), FolderPath);
	Params->TryGetStringField(TEXT("component"), Component);
	if (!Params->TryGetStringField(TEXT("object_type"), ObjectType) || ObjectType.IsEmpty())
	{
		ObjectType = TEXT("Vehicle");
	}

	if (BpPath.IsEmpty() && FolderPath.IsEmpty())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Provide either 'bp_path' or 'folder_path'"));
	}

	bool bDryRun = false;
	Params->TryGetBoolField(TEXT("dry_run"), bDryRun);

	bool bSkipNoCollision = true;
	if (Params->HasField(TEXT("skip_if_no_mesh_collision")))
	{
		Params->TryGetBoolField(TEXT("skip_if_no_mesh_collision"), bSkipNoCollision);
	}

	FCollisionApplyConfig Cfg;
	Cfg.ComponentFilter = Component;
	Cfg.bSkipIfNoMeshCollision = bSkipNoCollision;
	Cfg.bDryRun = bDryRun;

	// Resolve object type channel by display name.
	{
		ECollisionChannel Channel = ECC_WorldStatic;
		if (!ResolveCollisionChannelByName(ObjectType, Channel))
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unknown collision object type '%s'"), *ObjectType));
		}
		Cfg.ObjectType = Channel;
		Cfg.bSetObjectType = true;
	}

	// Parse responses map (channel display name -> Ignore/Overlap/Block).
	const TSharedPtr<FJsonObject>* RespObj = nullptr;
	if (Params->TryGetObjectField(TEXT("responses"), RespObj) && RespObj && RespObj->IsValid())
	{
		for (const auto& Pair : (*RespObj)->Values)
		{
			ECollisionChannel Channel = ECC_WorldStatic;
			if (!ResolveCollisionChannelByName(FString(*Pair.Key), Channel))
			{
				return FSmithUECommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Unknown response channel '%s'"), *Pair.Key));
			}
			FString RespStr;
			if (Pair.Value.IsValid())
			{
				RespStr = Pair.Value->AsString();
			}
			ECollisionResponse Resp = ECR_Block;
			if (!ParseCollisionResponse(RespStr, Resp))
			{
				return FSmithUECommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Invalid response '%s' for channel '%s' (use Ignore/Overlap/Block)"), *RespStr, *Pair.Key));
			}
			Cfg.Responses.Add(TPair<ECollisionChannel, ECollisionResponse>(Channel, Resp));
		}
	}

	// Collect target blueprint paths.
	TArray<FString> BpPaths;
	if (!BpPath.IsEmpty())
	{
		BpPaths.Add(BpPath);
	}
	if (!FolderPath.IsEmpty())
	{
		// Normalize content-browser virtual path -> real package path (handles project + plugins).
		FString Folder;
		FSmithUECommonUtils::NormalizeContentBrowserPath(FolderPath, Folder);

		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = false;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		ARM.Get().GetAssets(Filter, Assets);
		for (const FAssetData& Asset : Assets)
		{
			BpPaths.AddUnique(Asset.GetObjectPathString());
		}
	}

	if (BpPaths.Num() == 0)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("No Blueprints found for the given bp_path/folder_path"));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpSetCompCollision", "SmithUE: Set Component Collision"));

	TArray<TSharedPtr<FJsonValue>> BpResults;
	int32 TotalChanged = 0;
	for (const FString& Path : BpPaths)
	{
		BpResults.Add(MakeShared<FJsonValueObject>(ProcessBlueprintCollision(Path, Cfg, TotalChanged)));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("dry_run"), bDryRun);
	Data->SetStringField(TEXT("object_type"), ObjectType);
	Data->SetNumberField(TEXT("blueprint_count"), BpPaths.Num());
	Data->SetNumberField(TEXT("total_components_changed"), TotalChanged);
	Data->SetArrayField(TEXT("blueprints"), BpResults);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_override_function
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpOverrideFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("function_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, FunctionName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("function_name"), FunctionName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path"));
	}

	// Check if override graph already exists
	if (FindGraph(Blueprint, FunctionName))
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Override graph already exists for '%s'"), *FunctionName));
	}

	// Find the parent function to override
	UClass* ParentClass = Blueprint->ParentClass;
	if (!ParentClass)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint has no parent class"));
	}

	UFunction* ParentFunction = ParentClass->FindFunctionByName(FName(*FunctionName));
	if (!ParentFunction)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Function '%s' not found in parent class '%s' or its ancestors"),
				*FunctionName, *ParentClass->GetName()));
	}

	// Verify the function is overridable (BlueprintNativeEvent or BlueprintImplementableEvent)
	if (!ParentFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Function '%s' is not a BlueprintEvent and cannot be overridden in Blueprint"),
				*FunctionName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpOverrideFunction", "SmithUE: Override Blueprint Function"));

	// Create the override graph using UClass* overload — this properly sets up:
	// - Entry node with FunctionReference pointing to parent class
	// - CallParentFunction node (calls Super)
	// - Result node with correct pin types from parent signature
	// - All exec/data pins auto-connected
	UClass* FunctionOwnerClass = ParentFunction->GetOwnerClass();
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated=*/false, FunctionOwnerClass);

	// Find Entry/Result nodes created by the engine for response reporting
	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		if (!EntryNode) { EntryNode = Cast<UK2Node_FunctionEntry>(Node); }
		if (!ResultNode) { ResultNode = Cast<UK2Node_FunctionResult>(Node); }
	}

	// Build response with function signature info
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("graph_name"), FunctionName);
	Data->SetStringField(TEXT("parent_class"), ParentClass->GetName());

	// Report the pins on entry/result for caller reference
	if (EntryNode)
	{
		TArray<TSharedPtr<FJsonValue>> InputPins;
		for (UEdGraphPin* Pin : EntryNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
				FString Type = Pin->PinType.PinCategory.ToString();
				if (Pin->PinType.PinSubCategoryObject != nullptr)
				{
					Type += TEXT("/");
					Type += Pin->PinType.PinSubCategoryObject->GetName();
				}
				PinObj->SetStringField(TEXT("type"), Type);
				InputPins.Add(MakeShared<FJsonValueObject>(PinObj));
			}
		}
		if (InputPins.Num() > 0)
		{
			Data->SetArrayField(TEXT("inputs"), InputPins);
		}
	}
	if (ResultNode)
	{
		TArray<TSharedPtr<FJsonValue>> OutputPins;
		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
				FString Type = Pin->PinType.PinCategory.ToString();
				if (Pin->PinType.PinSubCategoryObject != nullptr)
				{
					Type += TEXT("/");
					Type += Pin->PinType.PinSubCategoryObject->GetName();
				}
				PinObj->SetStringField(TEXT("type"), Type);
				OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
			}
		}
		if (OutputPins.Num() > 0)
		{
			Data->SetArrayField(TEXT("outputs"), OutputPins);
		}
	}

	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpDescribeComponents(const TSharedPtr<FJsonObject>& Params)
{
	FString BpPath, FolderPath;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("folder_path"), FolderPath);
	BpPath.TrimStartAndEndInline();
	FolderPath.TrimStartAndEndInline();

	if (!BpPath.IsEmpty() && !FolderPath.IsEmpty())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Provide either folder_path or bp_path, not both"));
	}
	if (BpPath.IsEmpty() && FolderPath.IsEmpty())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Either folder_path or bp_path is required"));
	}

	bool bRecursive = false;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

	TArray<FString> BpPaths;
	if (!BpPath.IsEmpty())
	{
		BpPaths.Add(BpPath);
	}
	else
	{
		AppendBlueprintAssetsFromFolder(FolderPath, bRecursive, BpPaths);
	}

	TArray<TSharedPtr<FJsonValue>> BlueprintResults;
	for (const FString& Path : BpPaths)
	{
		UBlueprint* Blueprint = LoadBlueprint(Path);
		if (!Blueprint)
		{
			if (!BpPath.IsEmpty())
			{
				return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BpPath));
			}
			continue;
		}

		BlueprintResults.Add(MakeShared<FJsonValueObject>(DescribeBlueprintComponents(Blueprint)));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("total_blueprints"), BlueprintResults.Num());
	Data->SetArrayField(TEXT("blueprints"), BlueprintResults);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpBulkSetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString BpPath, FolderPath, ComponentClass, Component;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("folder_path"), FolderPath);
	Params->TryGetStringField(TEXT("component_class"), ComponentClass);
	Params->TryGetStringField(TEXT("component"), Component);

	if (BpPath.IsEmpty() && FolderPath.IsEmpty())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Provide either 'bp_path' or 'folder_path'"));
	}

	const TArray<TSharedPtr<FJsonValue>>* EditValues = nullptr;
	if (!Params->TryGetArrayField(TEXT("edits"), EditValues) || !EditValues || EditValues->Num() == 0)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required non-empty array param: 'edits'"));
	}

	FBulkApplyConfig Cfg;
	Cfg.ComponentClassFilter = ComponentClass;
		Cfg.ComponentFilter = Component;
		Params->TryGetBoolField(TEXT("dry_run"), Cfg.bDryRun);
		Params->TryGetBoolField(TEXT("defer_compile"), Cfg.bDeferCompile);
		Params->TryGetBoolField(TEXT("include_inherited"), Cfg.bIncludeInherited);

	for (const TSharedPtr<FJsonValue>& EditValue : *EditValues)
	{
		const TSharedPtr<FJsonObject>* EditObj = nullptr;
		if (!EditValue.IsValid() || !EditValue->TryGetObject(EditObj) || !EditObj || !EditObj->IsValid())
		{
			return FSmithUECommonUtils::CreateErrorResponse(TEXT("Each edits entry must be an object"));
		}

		FBulkComponentEdit Edit;
		if (!(*EditObj)->TryGetStringField(TEXT("property_path"), Edit.PropertyPath) || Edit.PropertyPath.IsEmpty())
		{
			return FSmithUECommonUtils::CreateErrorResponse(TEXT("Each edits entry requires non-empty 'property_path'"));
		}
		Edit.Value = (*EditObj)->Values.FindRef(TEXT("value"));
		if (!Edit.Value.IsValid())
		{
			return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Edit '%s' requires 'value'"), *Edit.PropertyPath));
		}
		Cfg.Edits.Add(Edit);
	}

	TArray<FString> BpPaths;
	if (!BpPath.IsEmpty())
	{
		BpPaths.Add(BpPath);
	}
	if (!FolderPath.IsEmpty())
	{
		// Normalize content-browser virtual path -> real package path (handles project + plugins).
		FString Folder;
		FSmithUECommonUtils::NormalizeContentBrowserPath(FolderPath, Folder);


		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = false;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		ARM.Get().GetAssets(Filter, Assets);
		for (const FAssetData& Asset : Assets)
		{
			BpPaths.AddUnique(Asset.GetObjectPathString());
		}
	}

	if (BpPaths.Num() == 0)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("No Blueprints found for the given bp_path/folder_path"));
	}

	TArray<TSharedPtr<FJsonValue>> BpResults;
	TArray<UBlueprint*> DeferredCompileBlueprints;
	int32 TotalBlueprintsChanged = 0;
	int32 TotalComponentsChanged = 0;
	int32 TotalEditsApplied = 0;
	for (const FString& Path : BpPaths)
	{
		BpResults.Add(MakeShared<FJsonValueObject>(ProcessBlueprintBulkComponentProperties(Path, Cfg, DeferredCompileBlueprints, TotalBlueprintsChanged, TotalComponentsChanged, TotalEditsApplied)));
	}

	if (!Cfg.bDryRun && Cfg.bDeferCompile && DeferredCompileBlueprints.Num() > 0)
	{
		FBlueprintCompilationManager::FlushCompilationQueueAndReinstance();
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("dry_run"), Cfg.bDryRun);
		Data->SetBoolField(TEXT("defer_compile"), Cfg.bDeferCompile);
		Data->SetBoolField(TEXT("include_inherited"), Cfg.bIncludeInherited);
	Data->SetNumberField(TEXT("blueprint_count"), BpPaths.Num());
	Data->SetNumberField(TEXT("total_blueprints_changed"), TotalBlueprintsChanged);
	Data->SetNumberField(TEXT("total_components_changed"), TotalComponentsChanged);
	Data->SetNumberField(TEXT("total_edits_applied"), TotalEditsApplied);
	Data->SetArrayField(TEXT("blueprints"), BpResults);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpCompile(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpCompile", "SmithUE: Compile Blueprint"));
	TArray<FString> CompileErrors;
	const bool bCompiled = CompileBlueprint(Blueprint, CompileErrors);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("compiled"), bCompiled);
	Data->SetBoolField(TEXT("has_errors"), CompileErrors.Num() > 0);
	AppendJsonStringArray(Data, TEXT("errors"), CompileErrors);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_reparent — Change the parent class of a Blueprint
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpReparent(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("new_parent_class") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, NewParentClassName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("new_parent_class"), NewParentClassName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	// Resolve the new parent class (try native class first, then Blueprint path)
	UClass* NewParentClass = ResolveClassByName(NewParentClassName, UObject::StaticClass(), TEXT('A'));
	if (!NewParentClass) { NewParentClass = ResolveClassByName(NewParentClassName, UObject::StaticClass(), TEXT('U')); }
	// Fallback: Blueprint path
	if (!NewParentClass && NewParentClassName.Contains(TEXT("/")))
	{
		FString BpPathToLoad = NewParentClassName;
		if (BpPathToLoad.EndsWith(TEXT("_C")))
		{
			int32 DotIdx;
			if (BpPathToLoad.FindLastChar(TEXT('.'), DotIdx)) { BpPathToLoad = BpPathToLoad.Left(DotIdx); }
		}
		if (UBlueprint* ParentBP = LoadBlueprint(BpPathToLoad))
		{
			NewParentClass = ParentBP->GeneratedClass;
		}
	}
	if (!NewParentClass)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("New parent class not found: %s"), *NewParentClassName));
	}

	// Validate: new parent must be compatible (Actor-based BP needs Actor parent, etc.)
	UClass* OldParentClass = Blueprint->ParentClass;
	if (NewParentClass == OldParentClass)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("New parent class is the same as current parent"));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpReparent", "SmithUE: Reparent Blueprint"));
	Blueprint->ParentClass = NewParentClass;
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), BpPath);
	Data->SetStringField(TEXT("old_parent"), OldParentClass ? OldParentClass->GetName() : TEXT("None"));
	Data->SetStringField(TEXT("new_parent"), NewParentClass->GetName());
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_copy_graph — Copy a function/event graph from one Blueprint to another
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpCopyGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("source_bp"), TEXT("target_bp"), TEXT("graph_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString SourceBpPath, TargetBpPath, GraphName, NewGraphName;
	Params->TryGetStringField(TEXT("source_bp"), SourceBpPath);
	Params->TryGetStringField(TEXT("target_bp"), TargetBpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	Params->TryGetStringField(TEXT("new_graph_name"), NewGraphName);
	if (NewGraphName.IsEmpty()) { NewGraphName = GraphName; }

	UBlueprint* SourceBP = LoadBlueprint(SourceBpPath);
	if (!SourceBP) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid source_bp path")); }
	UBlueprint* TargetBP = LoadBlueprint(TargetBpPath);
	if (!TargetBP) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid target_bp path")); }

	// Find source graph
	UEdGraph* SourceGraph = FindGraph(SourceBP, GraphName);
	if (!SourceGraph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' not found in source Blueprint"), *GraphName));
	}

	// Check if target already has a graph with this name
	bool bOverwrite = Params->HasField(TEXT("overwrite")) && Params->GetBoolField(TEXT("overwrite"));
	UEdGraph* ExistingGraph = FindGraph(TargetBP, NewGraphName);
	if (ExistingGraph && !bOverwrite)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' already exists in target Blueprint. Use overwrite=true to replace it."), *NewGraphName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpCopyGraph", "SmithUE: Copy Blueprint Graph"));

	// Determine graph type and clone accordingly
	bool bIsFunctionGraph = SourceBP->FunctionGraphs.Contains(SourceGraph);
	bool bIsUbergraph = SourceBP->UbergraphPages.Contains(SourceGraph);

	UEdGraph* ClonedGraph = nullptr;

	if (ExistingGraph && bOverwrite)
	{
		// OVERWRITE PATH: Preserve the existing graph object (maintains editor references)
		// and replace its CONTENTS with nodes from the source graph.
		// This avoids RemoveGraph re-outering the graph to the Package, which causes
		// FindBlueprintForNodeChecked assertions when the editor opens the graph.
		ExistingGraph->Modify();

		// 1. Clear all existing nodes from the target graph
		TArray<UEdGraphNode*> OldNodes = ExistingGraph->Nodes;
		for (UEdGraphNode* OldNode : OldNodes)
		{
			if (OldNode)
			{
				OldNode->Modify();
				OldNode->DestroyNode();
			}
		}
		ExistingGraph->Nodes.Empty();

		// 2. Export source graph's nodes to text (clipboard format preserves pin connections)
		TSet<UObject*> NodesToExport;
		for (UEdGraphNode* Node : SourceGraph->Nodes)
		{
			if (Node)
			{
				NodesToExport.Add(Node);
			}
		}
		FString ExportedText;
		FEdGraphUtilities::ExportNodesToText(NodesToExport, ExportedText);

		// 3. Import nodes into the existing graph (nodes get correct Outer = ExistingGraph)
		TSet<UEdGraphNode*> ImportedNodes;
		FEdGraphUtilities::ImportNodesFromText(ExistingGraph, ExportedText, ImportedNodes);

		ClonedGraph = ExistingGraph;
	}
	else
	{
		// NEW GRAPH PATH: No existing graph — clone normally
		ClonedGraph = FEdGraphUtilities::CloneGraph(SourceGraph, TargetBP, nullptr, false);
		if (!ClonedGraph)
		{
			return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to clone graph"));
		}

		// Ensure the cloned graph has the correct name (CloneGraph may auto-rename on collision)
		if (!ClonedGraph->GetName().Equals(NewGraphName, ESearchCase::CaseSensitive))
		{
			ClonedGraph->Rename(*NewGraphName, TargetBP, REN_DontCreateRedirectors);
		}

		// Add to the appropriate graph array in the target Blueprint
		if (bIsFunctionGraph)
		{
			TargetBP->FunctionGraphs.Add(ClonedGraph);
		}
		else if (bIsUbergraph)
		{
			TargetBP->UbergraphPages.Add(ClonedGraph);
		}
		else
		{
			// Default: treat as function graph
			TargetBP->FunctionGraphs.Add(ClonedGraph);
		}
	}

	// Verify the graph's Outer chain is correct (diagnostic ensure)
	ensure(ClonedGraph->GetOuter() == TargetBP);
	ensure(FBlueprintEditorUtils::FindBlueprintForGraph(ClonedGraph) == TargetBP);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBP);

	// Post-mark verification: ensure MarkBlueprintAsStructurallyModified didn't disrupt the Outer
	if (ClonedGraph->GetOuter() != TargetBP)
	{
		// Recovery: force re-outer the graph back to the Blueprint
		ClonedGraph->Rename(*ClonedGraph->GetName(), TargetBP, REN_DontCreateRedirectors);
	}

	// Count nodes for response
	int32 NodeCount = ClonedGraph->Nodes.Num();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("graph_name"), NewGraphName);
	Data->SetNumberField(TEXT("node_count"), NodeCount);
	Data->SetStringField(TEXT("source_bp"), SourceBpPath);
	Data->SetStringField(TEXT("target_bp"), TargetBpPath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRemoveGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, GraphName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' not found in Blueprint"), *GraphName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRemoveGraph", "SmithUE: Remove Blueprint Graph"));

	Blueprint->FunctionGraphs.Remove(Graph);
	Blueprint->UbergraphPages.Remove(Graph);
	FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removed_graph"), GraphName);
	Data->SetStringField(TEXT("bp_path"), BpPath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRenameGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("new_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, GraphName, NewName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	Params->TryGetStringField(TEXT("new_name"), NewName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' not found in Blueprint"), *GraphName));
	}

	// Check if new name already exists
	FName NewFName(*NewName);
	UEdGraph* Existing = FindGraph(Blueprint, NewName);
	if (Existing && Existing != Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("A graph named '%s' already exists"), *NewName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRenameGraph", "SmithUE: Rename Blueprint Graph"));

	FName OldFName = Graph->GetFName();

	// Rename the graph UObject
	Graph->Rename(*NewName, Graph->GetOuter(), REN_DontCreateRedirectors);

	// Update the FunctionEntry node's FunctionReference if this is a function graph
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			FMemberReference& Ref = Entry->FunctionReference;
			if (Ref.GetMemberName() == OldFName)
			{
				Ref.SetSelfMember(NewFName);
			}
			break;
		}
	}

	// Update all K2Node_CallFunction nodes in this Blueprint that reference the old function name
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	int32 UpdatedCallers = 0;
	for (UEdGraph* OtherGraph : AllGraphs)
	{
		if (!OtherGraph) { continue; }
		for (UEdGraphNode* Node : OtherGraph->Nodes)
		{
			UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
			if (!CallNode) { continue; }

			FMemberReference& FuncRef = CallNode->FunctionReference;
			if (FuncRef.GetMemberName() == OldFName)
			{
				// Check if it references Self or the generated class
				UClass* MemberParent = FuncRef.GetMemberParentClass();
				if (MemberParent == nullptr || MemberParent == Blueprint->SkeletonGeneratedClass || MemberParent == Blueprint->GeneratedClass)
				{
					FuncRef.SetSelfMember(NewFName);
					UpdatedCallers++;
				}
			}
		}
	}

	// Fix local variable scope references in the renamed graph
	// Local var Get/Set nodes store scope as a string (old graph name); update to new name.
	int32 FixedLocalVarScopes = 0;
	FString OldNameStr = OldFName.ToString();
	FString NewNameStr = NewFName.ToString();

	// Build GUID map from FunctionEntry's LocalVariables
	TMap<FName, FGuid> LocalVarGuids;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			for (const FBPVariableDescription& LocalVar : Entry->LocalVariables)
			{
				LocalVarGuids.Add(LocalVar.VarName, LocalVar.VarGuid);
			}
			break;
		}
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
		if (!VarNode) { continue; }
		FMemberReference& VarRef = VarNode->VariableReference;
		if (!VarRef.IsLocalScope()) { continue; }
		if (VarRef.GetMemberScopeName() == OldNameStr)
		{
			FName VarName = VarRef.GetMemberName();
			FGuid VarGuid = VarRef.GetMemberGuid();
			if (!VarGuid.IsValid())
			{
				if (FGuid* Found = LocalVarGuids.Find(VarName))
				{
					VarGuid = *Found;
				}
			}
			VarRef.SetLocalMember(VarName, NewNameStr, VarGuid);
			FixedLocalVarScopes++;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("old_name"), GraphName);
	Data->SetStringField(TEXT("new_name"), NewName);
	Data->SetNumberField(TEXT("updated_callers"), UpdatedCallers);
	Data->SetNumberField(TEXT("fixed_local_var_scopes"), FixedLocalVarScopes);
	Data->SetStringField(TEXT("bp_path"), BpPath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpFixupSelfReferences(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpFixupSelf", "SmithUE: Fixup Self References"));

	// STEP 1: Force skeleton regeneration so all variables and functions are available
	// This is critical because bp_add_variable and bp_copy_graph add entries without refreshing the skeleton
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	EBlueprintCompileOptions CompileOpts = EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::RegenerateSkeletonOnly;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileOpts);

	UClass* SkeletonClass = Blueprint->SkeletonGeneratedClass;
	if (!SkeletonClass) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Cannot get SkeletonGeneratedClass after regeneration")); }

	// Collect all function graph names in this Blueprint for function call fixup
	TSet<FName> LocalFunctionNames;
	for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
	{
		if (FuncGraph) { LocalFunctionNames.Add(FuncGraph->GetFName()); }
	}

	// Collect all variable names available via the skeleton class + SCS components + NewVariables
	TSet<FName> LocalVariableNames;
	for (TFieldIterator<FProperty> It(SkeletonClass); It; ++It)
	{
		LocalVariableNames.Add(It->GetFName());
	}
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		LocalVariableNames.Add(Var.VarName);
	}
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* SCSNode : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (SCSNode)
			{
				LocalVariableNames.Add(SCSNode->GetVariableName());
			}
		}
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	int32 FixedVarNodes = 0;
	int32 FixedFuncNodes = 0;
	int32 FixedEntryNodes = 0;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) { continue; }
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) { continue; }

		// Fix UK2Node_FunctionEntry - set FunctionReference to Self for NEW functions only.
		// After CloneGraph, entry nodes retain stale GUIDs from the source BP.
		// GetMemberParentClass() may return NULL (unresolvable GUID) or a foreign class.
		// SetSelfMember ensures pins reconstruct from the local skeleton.
		// BUT: skip override functions (e.g. UserConstructionScript) — calling SetSelfMember
		// on an override entry tells UE "I define this function" which conflicts with the parent.
		if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
		{
			FName GraphFName = Graph->GetFName();
			if (LocalFunctionNames.Contains(GraphFName))
			{
				// Check if this function is an override of a parent class function
				UFunction* ParentFunc = nullptr;
				if (UClass* SuperClass = SkeletonClass->GetSuperClass())
				{
					ParentFunc = SuperClass->FindFunctionByName(GraphFName);
				}
				if (!ParentFunc)
				{
					// Not an override — safe to fix
					EntryNode->FunctionReference.SetSelfMember(GraphFName);
					FixedEntryNodes++;
				}
			}
		}
		// Fix UK2Node_VariableGet
		if (UK2Node_VariableGet* VarGetNode = Cast<UK2Node_VariableGet>(Node))
		{
			FName VarName = VarGetNode->VariableReference.GetMemberName();
			if (LocalVariableNames.Contains(VarName) && !VarGetNode->VariableReference.IsLocalScope())
			{
				// ALWAYS force SetSelfMember for any variable get referencing a local variable.
				// After CloneGraph, self-context nodes retain stale GUIDs from the source BP.
				// This clears the GUID and forces name-based resolution.
				VarGetNode->VariableReference.SetSelfMember(VarName);
				FixedVarNodes++;
			}
		}
		// Fix UK2Node_VariableSet
		else if (UK2Node_VariableSet* VarSetNode = Cast<UK2Node_VariableSet>(Node))
		{
			FName VarName = VarSetNode->VariableReference.GetMemberName();
			if (LocalVariableNames.Contains(VarName) && !VarSetNode->VariableReference.IsLocalScope())
			{
				VarSetNode->VariableReference.SetSelfMember(VarName);
				FixedVarNodes++;
			}
		}
		// Fix UK2Node_CallFunction targeting self functions
		else if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			FName FuncName = CallNode->FunctionReference.GetMemberName();
			if (LocalFunctionNames.Contains(FuncName))
			{
				// ALWAYS force SetSelfMember for any call to a local function.
				// After CloneGraph, self-context nodes retain stale GUIDs from the source BP.
				// GetMemberParentClass() may resolve to SkeletonClass (appearing correct),
				// but the internal GUID won't match local function GUIDs, causing compile failures.
				// SetSelfMember clears the GUID and forces name-based resolution which always works.
				CallNode->FunctionReference.SetSelfMember(FuncName);
				FixedFuncNodes++;
			}
		}
		}
	}

	// STEP 2: Regenerate skeleton AGAIN after fixing references.
	// The first skeleton regen (before fixup) may have generated incorrect function signatures
	// because entry nodes still pointed to foreign classes. Now that entries are fixed,
	// regenerating the skeleton produces correct function signatures (including TMap parameters).
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileOpts);

	// STEP 3: Now refresh all nodes with the CORRECT skeleton - this reconstructs pins properly
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), BpPath);
	Data->SetNumberField(TEXT("fixed_entry_nodes"), FixedEntryNodes);
	Data->SetNumberField(TEXT("fixed_variable_nodes"), FixedVarNodes);
	Data->SetNumberField(TEXT("fixed_function_nodes"), FixedFuncNodes);
	Data->SetNumberField(TEXT("total_fixed"), FixedEntryNodes + FixedVarNodes + FixedFuncNodes);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpFixLocalVarScope(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpFixLocalVarScope", "SmithUE: Fix Local Variable Scopes"));

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	int32 TotalFixed = 0;
	TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) { continue; }

		FName GraphFName = Graph->GetFName();
		FString GraphNameStr = GraphFName.ToString();

		// Build GUID map from FunctionEntry's LocalVariables for this graph
		TMap<FName, FGuid> LocalVarGuids;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
			{
				for (const FBPVariableDescription& LocalVar : Entry->LocalVariables)
				{
					LocalVarGuids.Add(LocalVar.VarName, LocalVar.VarGuid);
				}
				break;
			}
		}
		if (LocalVarGuids.Num() == 0) { continue; } // No local vars declared here

		int32 FixedInGraph = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
			if (!VarNode) { continue; }
			FMemberReference& VarRef = VarNode->VariableReference;
			if (!VarRef.IsLocalScope()) { continue; }

			FName VarName = VarRef.GetMemberName();
			// Only fix if this variable is actually declared as local in THIS graph
			if (!LocalVarGuids.Contains(VarName)) { continue; }

			if (VarRef.GetMemberScopeName() != GraphNameStr)
			{
				FGuid VarGuid = VarRef.GetMemberGuid();
				if (!VarGuid.IsValid())
				{
					if (FGuid* Found = LocalVarGuids.Find(VarName))
					{
						VarGuid = *Found;
					}
				}
				VarRef.SetLocalMember(VarName, GraphNameStr, VarGuid);
				FixedInGraph++;
			}
		}

		if (FixedInGraph > 0)
		{
			Details->SetNumberField(GraphNameStr, FixedInGraph);
			TotalFixed += FixedInGraph;
		}
	}

	if (TotalFixed > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), BpPath);
	Data->SetNumberField(TEXT("total_fixed"), TotalFixed);
	Data->SetObjectField(TEXT("details"), Details);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
