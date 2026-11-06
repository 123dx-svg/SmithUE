// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEEditorCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"
#include "ComponentReregisterContext.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "SLevelViewport.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Misc/ScopedSlowTask.h"
#include "Editor/TransBuffer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "EditorAssetLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "FileHelpers.h"
#include "Materials/MaterialExpression.h"
#include "Misc/PackageName.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    /** Find an actor in the editor world by label (display name) or internal name. */
    AActor* FindActorByLabel(const FString& Label)
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World)
        {
            return nullptr;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (Actor && (Actor->GetActorLabel() == Label || Actor->GetName() == Label))
            {
                return Actor;
            }
        }
        return nullptr;
    }

    /** Build a compact JSON object describing an actor. */
    TSharedPtr<FJsonObject> ActorToJson(AActor* Actor)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
        Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

        const FVector Loc = Actor->GetActorLocation();
        TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
        LocObj->SetNumberField(TEXT("x"), Loc.X);
        LocObj->SetNumberField(TEXT("y"), Loc.Y);
        LocObj->SetNumberField(TEXT("z"), Loc.Z);
        Obj->SetObjectField(TEXT("location"), LocObj);

        const FRotator Rot = Actor->GetActorRotation();
        TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
        RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
        RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
        RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
        Obj->SetObjectField(TEXT("rotation"), RotObj);

        return Obj;
    }

    /** Read an {x,y,z} JSON object field into FVector. */
    FVector VectorFromJsonField(const TSharedPtr<FJsonObject>& Params, const FString& Field, FVector Default = FVector::ZeroVector)
    {
        const TSharedPtr<FJsonObject>* SubObj = nullptr;
        if (Params->TryGetObjectField(Field, SubObj) && SubObj)
        {
            double X = Default.X, Y = Default.Y, Z = Default.Z;
            (*SubObj)->TryGetNumberField(TEXT("x"), X);
            (*SubObj)->TryGetNumberField(TEXT("y"), Y);
            (*SubObj)->TryGetNumberField(TEXT("z"), Z);
            return FVector(X, Y, Z);
        }
        return Default;
    }

    /** Read a {pitch,yaw,roll} JSON object field into FRotator. */
    FRotator RotatorFromJsonField(const TSharedPtr<FJsonObject>& Params, const FString& Field, FRotator Default = FRotator::ZeroRotator)
    {
        const TSharedPtr<FJsonObject>* SubObj = nullptr;
        if (Params->TryGetObjectField(Field, SubObj) && SubObj)
        {
            double Pitch = Default.Pitch, Yaw = Default.Yaw, Roll = Default.Roll;
            (*SubObj)->TryGetNumberField(TEXT("pitch"), Pitch);
            (*SubObj)->TryGetNumberField(TEXT("yaw"), Yaw);
            (*SubObj)->TryGetNumberField(TEXT("roll"), Roll);
            return FRotator(Pitch, Yaw, Roll);
        }
        return Default;
    }
} // namespace

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEEditorCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    // spawn_actor
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("spawn_actor"),
            TEXT("Editor"),
            TEXT("Spawn an actor in the current level"),
            {
                FSmithUEToolParam(TEXT("class"),    TEXT("string"), TEXT("Actor class name (e.g. StaticMeshActor, PointLight)"), true),
                FSmithUEToolParam(TEXT("name"),     TEXT("string"), TEXT("Actor label shown in World Outliner")),
                FSmithUEToolParam(TEXT("location"), TEXT("object"), TEXT("Spawn location {x,y,z}")),
                FSmithUEToolParam(TEXT("rotation"), TEXT("object"), TEXT("Spawn rotation {pitch,yaw,roll}")),
                FSmithUEToolParam(TEXT("scale"),    TEXT("object"), TEXT("Scale {x,y,z}"))
            }),
        &HandleSpawnActor);

    // spawn_mesh_actor
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("spawn_mesh_actor"),
            TEXT("Editor"),
            TEXT("Spawn a StaticMeshActor into the current level with a mesh and optional material. Safe (no world switch)."),
            {
                FSmithUEToolParam(TEXT("mesh"),     TEXT("string"), TEXT("Static mesh asset path, e.g. /Engine/BasicShapes/Cube.Cube"), true),
                FSmithUEToolParam(TEXT("material"), TEXT("string"), TEXT("Optional material asset path applied to slot 0")),
                FSmithUEToolParam(TEXT("location"), TEXT("object"), TEXT("Spawn location {x,y,z}")),
                FSmithUEToolParam(TEXT("rotation"), TEXT("object"), TEXT("Spawn rotation {pitch,yaw,roll}")),
                FSmithUEToolParam(TEXT("scale"),    TEXT("object"), TEXT("Scale {x,y,z}")),
                FSmithUEToolParam(TEXT("label"),    TEXT("string"), TEXT("Actor label in World Outliner"))
            }),
        &HandleSpawnMeshActor);

    // get_all_actors
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_all_actors"),
            TEXT("Editor"),
            TEXT("List all actors in the current level"),
            {
                FSmithUEToolParam(TEXT("class_filter"), TEXT("string"), TEXT("Optional class name filter"))
            }),
        &HandleGetAllActors);

    // set_actor_property
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_actor_property"),
            TEXT("Editor"),
            TEXT("Set a reflected property on an actor by label"),
            {
                FSmithUEToolParam(TEXT("actor_label"),   TEXT("string"), TEXT("Actor label"), true),
                FSmithUEToolParam(TEXT("property_name"), TEXT("string"), TEXT("Property name"), true),
                FSmithUEToolParam(TEXT("value"),         TEXT("string"), TEXT("Property value (string/number/bool)"), true)
            }),
        &HandleSetActorProperty);

    // delete_actor
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("delete_actor"),
            TEXT("Editor"),
            TEXT("Delete an actor from the current level by label"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label"), true)
            }),
        &HandleDeleteActor);

    // add_postprocess_material
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("add_postprocess_material"),
            TEXT("Editor"),
            TEXT("Add a material to a PostProcessVolume's blendable list"),
            {
                FSmithUEToolParam(TEXT("actor_label"),   TEXT("string"), TEXT("PostProcessVolume actor label"), true),
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Material asset path (e.g. /Game/Materials/M_PP_ThermalVision)"), true),
                FSmithUEToolParam(TEXT("weight"),        TEXT("number"), TEXT("Blend weight (0.0-1.0, default 1.0)"))
            }),
        &HandleAddPostProcessMaterial);

    // open_map
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("open_map"),
            TEXT("Editor"),
            TEXT("Open a map asset in the Unreal Editor"),
            {
                FSmithUEToolParam(TEXT("map_path"), TEXT("string"), TEXT("Map asset path (e.g. /Game/Maps/MyMap)"), true)
            }),
        &HandleOpenMap);

    // get_project_setting
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_project_setting"),
            TEXT("Editor"),
            TEXT("Read a project configuration setting from INI file"),
            {
                FSmithUEToolParam(TEXT("section"),      TEXT("string"), TEXT("INI section name"), true),
                FSmithUEToolParam(TEXT("key"),          TEXT("string"), TEXT("INI key name"), true),
                FSmithUEToolParam(TEXT("config_file"),  TEXT("string"), TEXT("Config file: Engine (default), Editor, Game, Input"))
            }),
        &HandleGetProjectSetting);

    // set_project_setting
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_project_setting"),
            TEXT("Editor"),
            TEXT("Write a project configuration setting to INI file and flush to disk"),
            {
                FSmithUEToolParam(TEXT("section"),      TEXT("string"), TEXT("INI section name"), true),
                FSmithUEToolParam(TEXT("key"),          TEXT("string"), TEXT("INI key name"), true),
                FSmithUEToolParam(TEXT("value"),        TEXT("string"), TEXT("Value to set"), true),
                FSmithUEToolParam(TEXT("config_file"),  TEXT("string"), TEXT("Config file: Engine (default), Editor, Game, Input"))
            }),
        &HandleSetProjectSetting);

    // auto_layout_graph
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("auto_layout_graph"),
            TEXT("Editor"),
            TEXT("Auto-arrange nodes in any graph (Material, Blueprint, Niagara). Closes editor if open to prevent save conflicts."),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Full asset path (e.g. /Game/Materials/M_Test)"), true),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name filter (e.g. 'EventGraph'). For materials, omit.")),
                FSmithUEToolParam(TEXT("direction"), TEXT("string"), TEXT("Layout direction: 'left_to_right' (default) or 'top_to_bottom'")),
                FSmithUEToolParam(TEXT("spacing_x"), TEXT("number"), TEXT("Horizontal spacing between layers (default: 400)")),
                FSmithUEToolParam(TEXT("spacing_y"), TEXT("number"), TEXT("Vertical spacing between nodes in same layer (default: 200)"))
            }),
        &HandleAutoLayoutGraph);
}

// ---------------------------------------------------------------------------
// Command 1: spawn_actor
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    // --- required: class ---
    FString ClassName;
    if (!Params->TryGetStringField(TEXT("class"), ClassName) || ClassName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'class'"));
    }

    // --- find UClass by name ---
    UClass* ActorClass = nullptr;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (It->GetName() == ClassName && It->IsChildOf(AActor::StaticClass()))
        {
            ActorClass = *It;
            break;
        }
    }
    if (!ActorClass)
    {
        // Try loading it (handles short names like "StaticMeshActor")
        ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr,
            *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
    }
    if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown or non-actor class: '%s'"), *ClassName));
    }

    // --- world ---
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    // --- optional transform ---
    const FVector  Location = VectorFromJsonField(Params, TEXT("location"), FVector::ZeroVector);
    const FRotator Rotation = RotatorFromJsonField(Params, TEXT("rotation"), FRotator::ZeroRotator);
    const FVector  Scale    = VectorFromJsonField(Params, TEXT("scale"),    FVector::OneVector);

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));
    SpawnTransform.SetScale3D(Scale);

    // --- spawn inside a transaction ---
    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Spawn Actor")));

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
    if (!NewActor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("SpawnActor failed for class '%s'"), *ClassName));
    }

    // --- optional label (accept both "label" and "name") ---
    FString ActorName;
    if ((!Params->TryGetStringField(TEXT("label"), ActorName) || ActorName.IsEmpty()) &&
        (!Params->TryGetStringField(TEXT("name"),  ActorName) || ActorName.IsEmpty()))
    {
        ActorName.Empty();
    }
    if (!ActorName.IsEmpty())
    {
        NewActor->SetActorLabel(ActorName);
    }

    UE_LOG(LogSmithUE, Log, TEXT("spawn_actor: spawned '%s' (%s)"),
        *NewActor->GetActorLabel(), *ClassName);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
    Data->SetStringField(TEXT("class"), ClassName);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 1b: spawn_mesh_actor
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleSpawnMeshActor(const TSharedPtr<FJsonObject>& Params)
{
    FString MeshPath;
    if (!Params->TryGetStringField(TEXT("mesh"), MeshPath) || MeshPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'mesh'"));
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
    if (!Mesh)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Static mesh not found: '%s'"), *MeshPath));
    }

    const FVector  Location = VectorFromJsonField(Params, TEXT("location"), FVector::ZeroVector);
    const FRotator Rotation = RotatorFromJsonField(Params, TEXT("rotation"), FRotator::ZeroRotator);
    const FVector  Scale    = VectorFromJsonField(Params, TEXT("scale"),    FVector::OneVector);

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));
    SpawnTransform.SetScale3D(Scale);

    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Spawn Mesh Actor")));

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AStaticMeshActor* NewActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), SpawnTransform, SpawnParams);
    if (!NewActor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("SpawnActor failed for AStaticMeshActor"));
    }

    FString Label;
    if ((Params->TryGetStringField(TEXT("label"), Label) || Params->TryGetStringField(TEXT("name"), Label)) && !Label.IsEmpty())
    {
        NewActor->SetActorLabel(Label);
    }

    FString MaterialPath;
    bool bMaterialApplied = false;
    if (UStaticMeshComponent* Comp = NewActor->GetStaticMeshComponent())
    {
        Comp->SetStaticMesh(Mesh);
        if (Params->TryGetStringField(TEXT("material"), MaterialPath) && !MaterialPath.IsEmpty())
        {
            if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
            {
                Comp->SetMaterial(0, Mat);
                bMaterialApplied = true;
            }
        }
    }

    UE_LOG(LogSmithUE, Log, TEXT("spawn_mesh_actor: spawned '%s' mesh='%s' material_applied=%d"),
        *NewActor->GetActorLabel(), *MeshPath, bMaterialApplied);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
    Data->SetStringField(TEXT("mesh"), MeshPath);
    Data->SetStringField(TEXT("material"), MaterialPath);
    Data->SetBoolField(TEXT("material_applied"), bMaterialApplied);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 2: get_all_actors
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleGetAllActors(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    FString ClassFilter;
    Params->TryGetStringField(TEXT("class_filter"), ClassFilter);

    TArray<TSharedPtr<FJsonValue>> ActorArray;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor)
        {
            continue;
        }
        if (!ClassFilter.IsEmpty())
        {
            const FString ActorClassName = Actor->GetClass()->GetName();
            if (!ActorClassName.Contains(ClassFilter))
            {
                continue;
            }
        }
        ActorArray.Add(MakeShared<FJsonValueObject>(ActorToJson(Actor)));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("actors"), ActorArray);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 3: set_actor_property
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel;
    if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel) || ActorLabel.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'actor_label'"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName) || PropertyName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'property_name'"));
    }

    if (!Params->HasField(TEXT("value")))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'value'"));
    }

    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel));
    }

    // Try actor first, then fall through to components
    FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
    UObject* PropOwner = Actor;
    if (!Prop)
    {
        for (UActorComponent* Comp : Actor->GetComponents())
        {
            if (!Comp) continue;
            Prop = Comp->GetClass()->FindPropertyByName(FName(*PropertyName));
            if (Prop) { PropOwner = Comp; break; }
        }
    }
    if (!Prop)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Property not found: '%s' on actor '%s'"),
                *PropertyName, *ActorLabel));
    }

    // Reject unsupported complex property types
    if (Prop->IsA<FMapProperty>() || Prop->IsA<FSetProperty>() ||
        Prop->IsA<FDelegateProperty>() || Prop->IsA<FMulticastDelegateProperty>() ||
        Prop->IsA<FSoftObjectProperty>() || Prop->IsA<FSoftClassProperty>())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Complex property type not supported for '%s'"), *PropertyName));
    }

    TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("value"));
    void* PropAddr = Prop->ContainerPtrToValuePtr<void>(PropOwner);

    // Capture before value
    FString BeforeValue;
    Prop->ExportTextItem_Direct(BeforeValue, PropAddr, nullptr, PropOwner, PPF_None);

    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Set Actor Property")));
    Actor->Modify();
    if (PropOwner != Actor)
    {
        CastChecked<UActorComponent>(PropOwner)->Modify();
    }

    FText ImportError;
    bool bSuccess = false;

    // Convert JSON value to string for ImportText
    FString ValueStr;
    if (!JsonValue.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'value' parameter is missing or null"));
    }
    if (JsonValue->Type == EJson::String) ValueStr = JsonValue->AsString();
    else if (JsonValue->Type == EJson::Number) ValueStr = FString::SanitizeFloat(JsonValue->AsNumber());
    else if (JsonValue->Type == EJson::Boolean) ValueStr = JsonValue->AsBool() ? TEXT("True") : TEXT("False");

    // Use ImportText as primary method
    if (!ValueStr.IsEmpty())
    {
        const TCHAR* Result = Prop->ImportText_Direct(*ValueStr, PropAddr, PropOwner, PPF_None);
        bSuccess = (Result != nullptr);
    }

    // Fallback: direct type-specific setting for bitfield bools
    if (!bSuccess && Prop->IsA<FBoolProperty>())
    {
        CastField<FBoolProperty>(Prop)->SetPropertyValue(PropAddr, JsonValue->AsBool());
        bSuccess = true;
    }

    if (!bSuccess)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to set property '%s': %s"),
                *PropertyName, *ImportError.ToString()));
    }

    // Special handling: bHidden needs SetActorHiddenInGame to propagate to components
    if (PropertyName == TEXT("bHidden"))
    {
        Actor->SetActorHiddenInGame(JsonValue->AsBool());
    }

    // Propagate visual update: re-register the owning component to rebuild
    // its render proxy and recalculate world transform. This avoids
    // Actor::PostEditChangeProperty which triggers RerunConstructionScripts
    // and reverts simple property values.
    if (UActorComponent* Comp = Cast<UActorComponent>(PropOwner))
    {
        FComponentReregisterContext ReregCtx(Comp);
    }

    // Mark dirty + force viewport redraw
    Actor->MarkPackageDirty();
    if (GEditor)
    {
        GEditor->RedrawLevelEditingViewports();
    }

    FString AfterValue;
    Prop->ExportTextItem_Direct(AfterValue, PropAddr, nullptr, PropOwner, PPF_None);

    UE_LOG(LogSmithUE, Log, TEXT("set_actor_property: '%s'.%s set successfully"), *ActorLabel, *PropertyName);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_label"), ActorLabel);
    Data->SetStringField(TEXT("property_name"), PropertyName);
    Data->SetStringField(TEXT("before"), BeforeValue);
    Data->SetStringField(TEXT("after"), AfterValue);
    Data->SetBoolField(TEXT("changed"), BeforeValue != AfterValue);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 4: delete_actor
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel;
    if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel) || ActorLabel.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'actor_label'"));
    }

    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel));
    }

    UWorld* World = Actor->GetWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get world from actor"));
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Delete Actor")));

    UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
    bool bDeleted = false;
    if (ActorSubsystem)
    {
        bDeleted = ActorSubsystem->DestroyActor(Actor);
    }
    else
    {
        bDeleted = World->EditorDestroyActor(Actor, true);
    }

    if (!bDeleted)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to delete actor: '%s'"), *ActorLabel));
    }

    UE_LOG(LogSmithUE, Log, TEXT("delete_actor: deleted '%s'"), *ActorLabel);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("deleted_actor_label"), ActorLabel);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 5: get_viewport_info
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleGetViewportInfo(const TSharedPtr<FJsonObject>& Params)
{
    FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
    if (!LevelEditorModule)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("LevelEditor module not available"));
    }

    TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule->GetFirstActiveViewport();
    if (!ActiveViewport.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No active level editor viewport found"));
    }

    FEditorViewportClient& Client = static_cast<FEditorViewportClient&>(ActiveViewport->GetAssetViewportClient());
    const FVector  ViewLoc = Client.GetViewLocation();
    const FRotator ViewRot = Client.GetViewRotation();
    const float    FOV     = Client.ViewFOV;

    TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
    LocObj->SetNumberField(TEXT("x"), ViewLoc.X);
    LocObj->SetNumberField(TEXT("y"), ViewLoc.Y);
    LocObj->SetNumberField(TEXT("z"), ViewLoc.Z);

    TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
    RotObj->SetNumberField(TEXT("pitch"), ViewRot.Pitch);
    RotObj->SetNumberField(TEXT("yaw"),   ViewRot.Yaw);
    RotObj->SetNumberField(TEXT("roll"),  ViewRot.Roll);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetObjectField(TEXT("location"), LocObj);
    Data->SetObjectField(TEXT("rotation"), RotObj);
    Data->SetNumberField(TEXT("fov"), FOV);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 6: add_postprocess_material
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleAddPostProcessMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel;
    if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel) || ActorLabel.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'actor_label'"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'material_path'"));
    }

    double Weight = 1.0;
    Params->TryGetNumberField(TEXT("weight"), Weight);

    // Find actor
    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel));
    }

    APostProcessVolume* PPV = Cast<APostProcessVolume>(Actor);
    if (!PPV)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor '%s' is not a PostProcessVolume (class: %s)"),
                *ActorLabel, *Actor->GetClass()->GetName()));
    }

    // Load material
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found at path: '%s'"), *MaterialPath));
    }

    // Add to blendables
    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Add PostProcess Material")));
    PPV->Modify();

    FWeightedBlendable Entry;
    Entry.Weight = (float)Weight;
    Entry.Object = Material;
    PPV->Settings.WeightedBlendables.Array.Add(Entry);

    // Mark dirty + force viewport redraw
     PPV->MarkPackageDirty();
     if (GEditor)
     {
         GEditor->RedrawLevelEditingViewports();
     }

     UE_LOG(LogSmithUE, Log, TEXT("add_postprocess_material: added '%s' to '%s' (weight=%.2f)"),
         *MaterialPath, *ActorLabel, Weight);

     TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
     Data->SetStringField(TEXT("actor_label"), ActorLabel);
     Data->SetStringField(TEXT("material_path"), MaterialPath);
     Data->SetNumberField(TEXT("weight"), Weight);
     Data->SetNumberField(TEXT("blendable_count"), PPV->Settings.WeightedBlendables.Array.Num());
     return FSmithUECommonUtils::CreateSuccessResponse(Data);
 }

// ---------------------------------------------------------------------------
// Command 7: open_map
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleOpenMap(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid parameters"));
    }

    FString MapPath;
    if (!Params->TryGetStringField(TEXT("map_path"), MapPath) || MapPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'map_path'"));
    }

    FString FilePath;
    if (!FPackageName::TryConvertLongPackageNameToFilename(MapPath, FilePath, FPackageName::GetMapPackageExtension()))
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid map path: %s"), *MapPath));
    }

    // LoadMap destroys the current world — calling it from inside a Tick-dispatched
    // TaskGraph task causes a TickTaskLevel assertion crash. Defer to next frame.
    FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([FilePath](float) -> bool
        {
            FEditorFileUtils::LoadMap(FilePath);
            if (GEditor)
            {
                GEditor->RedrawAllViewports();
            }
            return false; // one-shot
        }),
        0.0f);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("map_path"), MapPath);
    Data->SetStringField(TEXT("status"), TEXT("pending — map will open next frame"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

 // ---------------------------------------------------------------------------
 // Command 7: get_project_setting
 // ---------------------------------------------------------------------------

 TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleGetProjectSetting(const TSharedPtr<FJsonObject>& Params)
 {
     if (!Params)
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid parameters"));
     }

     FString Section, Key, ConfigFileName;
     if (!Params->TryGetStringField(TEXT("section"), Section) || Section.IsEmpty())
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing or empty 'section' parameter"));
     }
     if (!Params->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty())
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing or empty 'key' parameter"));
     }
     Params->TryGetStringField(TEXT("config_file"), ConfigFileName);

     // Resolve config file path
     auto ResolveConfigFile = [](const FString& Name) -> FString {
         if (Name.Equals(TEXT("Editor"), ESearchCase::IgnoreCase)) return GEditorIni;
         if (Name.Equals(TEXT("Game"), ESearchCase::IgnoreCase)) return GGameIni;
         if (Name.Equals(TEXT("Input"), ESearchCase::IgnoreCase)) return GInputIni;
         return GEngineIni; // default
     };

     FString ConfigFile = ResolveConfigFile(ConfigFileName);
     FString OutValue;
     bool bFound = GConfig->GetString(*Section, *Key, OutValue, ConfigFile);

     TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
     Data->SetStringField(TEXT("section"), Section);
     Data->SetStringField(TEXT("key"), Key);
     Data->SetStringField(TEXT("value"), OutValue);
     Data->SetBoolField(TEXT("found"), bFound);

     UE_LOG(LogSmithUE, Log, TEXT("get_project_setting: [%s] %s = '%s' (found=%d)"),
         *Section, *Key, *OutValue, bFound);

     return FSmithUECommonUtils::CreateSuccessResponse(Data);
 }

 // ---------------------------------------------------------------------------
 // Command 8: set_project_setting
 // ---------------------------------------------------------------------------

 TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleSetProjectSetting(const TSharedPtr<FJsonObject>& Params)
 {
     if (!Params)
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid parameters"));
     }

     FString Section, Key, Value, ConfigFileName;
     if (!Params->TryGetStringField(TEXT("section"), Section) || Section.IsEmpty())
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing or empty 'section' parameter"));
     }
     if (!Params->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty())
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing or empty 'key' parameter"));
     }
     if (!Params->TryGetStringField(TEXT("value"), Value))
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter"));
     }
     Params->TryGetStringField(TEXT("config_file"), ConfigFileName);

     // Resolve config file path
     auto ResolveConfigFile = [](const FString& Name) -> FString {
         if (Name.Equals(TEXT("Editor"), ESearchCase::IgnoreCase)) return GEditorIni;
         if (Name.Equals(TEXT("Game"), ESearchCase::IgnoreCase)) return GGameIni;
         if (Name.Equals(TEXT("Input"), ESearchCase::IgnoreCase)) return GInputIni;
         return GEngineIni; // default
     };

     FString ConfigFile = ResolveConfigFile(ConfigFileName);

     // Read old value for logging
     FString OldValue;
     GConfig->GetString(*Section, *Key, OldValue, ConfigFile);

     // Write new value
     GConfig->SetString(*Section, *Key, *Value, ConfigFile);

     // CRITICAL: Flush to disk
     GConfig->Flush(false, ConfigFile);

     // Optionally apply CVar at runtime if it exists
     IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Key);
     if (CVar)
     {
         CVar->Set(*Value);
     }

     UE_LOG(LogSmithUE, Log, TEXT("set_project_setting: [%s] %s = '%s' (was '%s')"),
         *Section, *Key, *Value, *OldValue);

     TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
     Data->SetBoolField(TEXT("persisted"), true);

     return FSmithUECommonUtils::CreateSuccessResponse(Data);
 }

// ---------------------------------------------------------------------------
// auto_layout_graph
// ---------------------------------------------------------------------------

namespace GraphLayoutInternal
{
    struct FLayoutNode
    {
        int32 Id;
        int32 Layer = -1;
        TArray<int32> Predecessors;
        TArray<int32> Successors;
    };

    void AssignLayers(TArray<FLayoutNode>& Nodes)
    {
        TQueue<int32> Queue;
        for (FLayoutNode& N : Nodes)
        {
            if (N.Successors.Num() == 0)
            {
                N.Layer = 0;
                Queue.Enqueue(N.Id);
            }
        }
        if (Queue.IsEmpty() && Nodes.Num() > 0)
        {
            Nodes[0].Layer = 0;
            Queue.Enqueue(0);
        }

        while (!Queue.IsEmpty())
        {
            int32 CurrentId;
            Queue.Dequeue(CurrentId);
            FLayoutNode& Current = Nodes[CurrentId];
            for (int32 PredId : Current.Predecessors)
            {
                int32 NewLayer = Current.Layer + 1;
                if (Nodes[PredId].Layer < NewLayer)
                {
                    Nodes[PredId].Layer = NewLayer;
                    Queue.Enqueue(PredId);
                }
            }
        }

        int32 MaxLayer = 0;
        for (const FLayoutNode& N : Nodes)
        {
            if (N.Layer >= 0) MaxLayer = FMath::Max(MaxLayer, N.Layer);
        }
        for (FLayoutNode& N : Nodes)
        {
            if (N.Layer < 0) N.Layer = MaxLayer + 1;
        }
    }

    /** Estimate visual height of a material expression node in pixels. */
    float EstimateNodeHeight(UMaterialExpression* Expr)
    {
        if (!Expr) return 100.f;
        // Custom nodes are tall due to code preview + many inputs
        int32 NumInputs = 0;
        for (FExpressionInputIterator Input(Expr); Input; ++Input)
        {
            ++NumInputs;
        }
        float BaseHeight = 80.f;
        float PerInputHeight = 28.f;
        float Height = BaseHeight + NumInputs * PerInputHeight;
        // Custom expression nodes with code preview are extra tall
        FString ClassName = Expr->GetClass()->GetName();
        if (ClassName.Contains(TEXT("Custom")))
        {
            Height = FMath::Max(Height, 350.f);
        }
        else if (ClassName.Contains(TEXT("CollectionParameter")) || ClassName.Contains(TEXT("TextureSample")))
        {
            Height = FMath::Max(Height, 150.f);
        }
        return Height;
    }

    /**
     * DFS tree-based layout for material graphs.
     *
     * Material graphs are near-trees: Material Result is the root, each pin
     * (BaseColor, EmissiveColor, Opacity, ...) drives a mostly-independent
     * subtree.  Shared nodes (e.g. ConeGradient feeding both emissive and
     * opacity paths) are centered between their consumers after the initial
     * tree layout pass.
     *
     * Algorithm:
     *   1. Build directed adjacency (predecessor = input source).
     *   2. Assign layers via BFS from sinks (X axis positioning).
     *   3. Identify "roots" — expressions directly connected to Material
     *      Result pins, in top-to-bottom visual order.
     *   4. DFS from each root backward through predecessors:
     *      a. Compute subtree height recursively (each leaf = own height;
     *         interior = max(own, sum of children)).
     *      b. Assign Y positions top-down: each child gets a contiguous
     *         vertical slice; the node itself is centered in its slice.
     *   5. Shared nodes (reached by multiple DFS paths): reposition at the
     *      centroid of their consumers' Y positions.
     *   6. Orphan nodes (no connections): placed in a row below the graph.
     */
    int32 LayoutMaterial(UMaterial* Material, bool bLeftToRight, float SpacingX, float SpacingY)
    {
        const auto Expressions = Material->GetExpressions();
        const int32 NumExprs = Expressions.Num();
        if (NumExprs == 0) return 0;

        // --- Build adjacency ---
        TArray<FLayoutNode> Nodes;
        Nodes.SetNum(NumExprs);
        for (int32 i = 0; i < NumExprs; ++i) Nodes[i].Id = i;

        TMap<UMaterialExpression*, int32> ExprToIndex;
        for (int32 i = 0; i < NumExprs; ++i)
        {
            if (Expressions[i]) ExprToIndex.Add(Expressions[i], i);
        }

        for (int32 i = 0; i < NumExprs; ++i)
        {
            UMaterialExpression* Expr = Expressions[i];
            if (!Expr) continue;
            for (FExpressionInputIterator Input(Expr); Input; ++Input)
            {
                if (Input->Expression)
                {
                    int32* PredIdx = ExprToIndex.Find(Input->Expression);
                    if (PredIdx)
                    {
                        Nodes[i].Predecessors.AddUnique(*PredIdx);
                        Nodes[*PredIdx].Successors.AddUnique(i);
                    }
                }
            }
        }

        // --- Separate connected vs orphan ---
        TArray<int32> ConnectedNodes;
        TArray<int32> OrphanNodes;
        for (int32 i = 0; i < NumExprs; ++i)
        {
            if (Nodes[i].Predecessors.Num() > 0 || Nodes[i].Successors.Num() > 0)
            {
                ConnectedNodes.Add(i);
            }
            else
            {
                OrphanNodes.Add(i);
            }
        }

        if (ConnectedNodes.Num() == 0)
        {
            // All orphans — just grid them
            float X = 0.f, Y = 0.f;
            for (int32 Idx : OrphanNodes)
            {
                if (!Expressions[Idx]) continue;
                Expressions[Idx]->MaterialExpressionEditorX = static_cast<int32>(X);
                Expressions[Idx]->MaterialExpressionEditorY = static_cast<int32>(Y);
                X += 250.f;
            }
            return NumExprs;
        }

        // --- Assign layers (for X positioning) ---
        AssignLayers(Nodes);

        int32 MaxLayer = 0;
        for (int32 Idx : ConnectedNodes)
        {
            MaxLayer = FMath::Max(MaxLayer, Nodes[Idx].Layer);
        }

        // --- Identify roots in Material Result pin order (top to bottom) ---
        // Only true sinks (no expression successors) connected to Material Result
        // pins become DFS roots. Non-sink nodes connected to Material Result will be
        // reached naturally via DFS from actual sinks that consume them.
        TArray<int32> RootOrder;
        auto* EditorData = Material->GetEditorOnlyData();
        if (EditorData)
        {
            auto TryAddRoot = [&](FExpressionInput& MatInput)
            {
                if (MatInput.Expression)
                {
                    int32* Idx = ExprToIndex.Find(MatInput.Expression);
                    if (Idx && Nodes[*Idx].Successors.Num() == 0)
                    {
                        RootOrder.AddUnique(*Idx);
                    }
                }
            };
            TryAddRoot(EditorData->BaseColor);
            TryAddRoot(EditorData->Metallic);
            TryAddRoot(EditorData->Roughness);
            TryAddRoot(EditorData->Normal);
            TryAddRoot(EditorData->EmissiveColor);
            TryAddRoot(EditorData->Opacity);
            TryAddRoot(EditorData->OpacityMask);
        }

        // Add any remaining sink nodes (layer 0, have predecessors but no successors)
        for (int32 Idx : ConnectedNodes)
        {
            if (Nodes[Idx].Successors.Num() == 0)
            {
                RootOrder.AddUnique(Idx);
            }
        }

        // If somehow no roots found, pick all layer-0 nodes
        if (RootOrder.Num() == 0)
        {
            for (int32 Idx : ConnectedNodes)
            {
                if (Nodes[Idx].Layer == 0) RootOrder.Add(Idx);
            }
        }

        // --- DFS subtree height computation ---
        // Height of a subtree = space it needs vertically.
        // Leaf node: own visual height + gap.
        // Interior node: max(own height, sum of children subtree heights).
        // Shared nodes (already visited): return 0 (they're placed elsewhere).
        const float NodeGap = 40.f;
        TMap<int32, float> SubtreeHeights;
        TSet<int32> HeightVisited;

        auto ComputeSubtreeHeight = [&](auto& Self, int32 NodeId) -> float
        {
            if (HeightVisited.Contains(NodeId)) return 0.f;
            HeightVisited.Add(NodeId);

            float OwnHeight = EstimateNodeHeight(Expressions[NodeId]) + NodeGap;
            const TArray<int32>& Preds = Nodes[NodeId].Predecessors;

            if (Preds.Num() == 0)
            {
                SubtreeHeights.Add(NodeId, OwnHeight);
                return OwnHeight;
            }

            float ChildrenTotal = 0.f;
            for (int32 P : Preds)
            {
                ChildrenTotal += Self(Self, P);
            }

            float Height = FMath::Max(OwnHeight, ChildrenTotal);
            SubtreeHeights.Add(NodeId, Height);
            return Height;
        };

        for (int32 Root : RootOrder)
        {
            ComputeSubtreeHeight(ComputeSubtreeHeight, Root);
        }

        // Pick up any connected nodes not reachable from identified roots
        for (int32 Idx : ConnectedNodes)
        {
            if (!HeightVisited.Contains(Idx))
            {
                ComputeSubtreeHeight(ComputeSubtreeHeight, Idx);
                RootOrder.AddUnique(Idx);
            }
        }

        // --- DFS position assignment ---
        // Each node is centered in its vertical slice.
        // Children are stacked vertically within the parent's slice.
        TSet<int32> Positioned;
        TArray<int32> SharedNodes;

        auto AssignYPositions = [&](auto& Self, int32 NodeId, float YStart, float YEnd) -> void
        {
            if (Positioned.Contains(NodeId))
            {
                SharedNodes.AddUnique(NodeId);
                return;
            }
            Positioned.Add(NodeId);

            // Center this node within its allocated range
            float NodeH = EstimateNodeHeight(Expressions[NodeId]);
            float CenterY = (YStart + YEnd) * 0.5f - NodeH * 0.5f;
            Expressions[NodeId]->MaterialExpressionEditorY = static_cast<int32>(CenterY);

            const TArray<int32>& Preds = Nodes[NodeId].Predecessors;
            if (Preds.Num() == 0) return;

            // Compute actual available height for children
            // (only unvisited children contribute height)
            float TotalChildH = 0.f;
            TArray<float> ChildHeights;
            ChildHeights.SetNum(Preds.Num());
            for (int32 i = 0; i < Preds.Num(); ++i)
            {
                int32 P = Preds[i];
                if (Positioned.Contains(P))
                {
                    ChildHeights[i] = 0.f;
                }
                else
                {
                    float H = SubtreeHeights.Contains(P) ? SubtreeHeights[P] : (EstimateNodeHeight(Expressions[P]) + NodeGap);
                    ChildHeights[i] = H;
                    TotalChildH += H;
                }
            }

            // Distribute children vertically, centered in our range
            float ChildStart = (YStart + YEnd) * 0.5f - TotalChildH * 0.5f;
            for (int32 i = 0; i < Preds.Num(); ++i)
            {
                if (ChildHeights[i] <= 0.f)
                {
                    // Already positioned (shared) — just mark it
                    SharedNodes.AddUnique(Preds[i]);
                    continue;
                }
                Self(Self, Preds[i], ChildStart, ChildStart + ChildHeights[i]);
                ChildStart += ChildHeights[i];
            }
        };

        // Assign each root's subtree a contiguous vertical slot
        float GlobalY = 0.f;
        for (int32 Root : RootOrder)
        {
            float RootH = SubtreeHeights.Contains(Root) ? SubtreeHeights[Root] : 200.f;
            AssignYPositions(AssignYPositions, Root, GlobalY, GlobalY + RootH);
            GlobalY += RootH + 80.f; // gap between separate trees
        }

        // --- Shared node fixup ---
        // Reposition shared nodes at the centroid of their consumers' Y positions
        for (int32 SharedId : SharedNodes)
        {
            if (!Expressions[SharedId]) continue;
            float SumY = 0.f;
            int32 Count = 0;
            for (int32 SuccId : Nodes[SharedId].Successors)
            {
                if (Positioned.Contains(SuccId) && Expressions[SuccId])
                {
                    SumY += Expressions[SuccId]->MaterialExpressionEditorY;
                    Count++;
                }
            }
            if (Count > 0)
            {
                Expressions[SharedId]->MaterialExpressionEditorY = static_cast<int32>(SumY / Count);
            }
        }

        // --- Assign X positions from layer ---
        for (int32 Idx : ConnectedNodes)
        {
            UMaterialExpression* Expr = Expressions[Idx];
            if (!Expr) continue;
            if (bLeftToRight)
            {
                Expr->MaterialExpressionEditorX = static_cast<int32>((MaxLayer - Nodes[Idx].Layer) * SpacingX);
            }
            else
            {
                // Top-to-bottom: swap axes
                int32 SavedY = Expr->MaterialExpressionEditorY;
                Expr->MaterialExpressionEditorX = SavedY;
                Expr->MaterialExpressionEditorY = static_cast<int32>((MaxLayer - Nodes[Idx].Layer) * SpacingY);
            }
        }

        // --- Orphan nodes in a row below ---
        if (OrphanNodes.Num() > 0)
        {
            // Find the bottom extent of the positioned graph
            float MaxY = 0.f;
            for (int32 Idx : ConnectedNodes)
            {
                if (Expressions[Idx])
                {
                    float Bottom = Expressions[Idx]->MaterialExpressionEditorY + EstimateNodeHeight(Expressions[Idx]);
                    MaxY = FMath::Max(MaxY, Bottom);
                }
            }

            float OrphanY = MaxY + 120.f;
            float OrphanX = 0.f;
            for (int32 Idx : OrphanNodes)
            {
                UMaterialExpression* Expr = Expressions[Idx];
                if (!Expr) continue;
                Expr->MaterialExpressionEditorX = static_cast<int32>(OrphanX);
                Expr->MaterialExpressionEditorY = static_cast<int32>(OrphanY);
                OrphanX += 250.f;
            }
        }

        // Diagnostic logging
        UE_LOG(LogSmithUE, Log, TEXT("LayoutMaterial: %d exprs, %d connected, %d orphans, %d roots, MaxLayer=%d"),
            NumExprs, ConnectedNodes.Num(), OrphanNodes.Num(), RootOrder.Num(), MaxLayer);
        for (int32 i = 0; i < NumExprs; ++i)
        {
            if (!Expressions[i]) continue;
            FString PredStr;
            for (int32 P : Nodes[i].Predecessors) PredStr += FString::Printf(TEXT("%d,"), P);
            FString SuccStr;
            for (int32 S : Nodes[i].Successors) SuccStr += FString::Printf(TEXT("%d,"), S);
            float H = SubtreeHeights.Contains(i) ? SubtreeHeights[i] : -1.f;
            UE_LOG(LogSmithUE, Log, TEXT("  [%d] %s Layer=%d Pos=(%d,%d) Preds=[%s] Succs=[%s] SubH=%.0f %s"),
                i, *Expressions[i]->GetClass()->GetName(), Nodes[i].Layer,
                Expressions[i]->MaterialExpressionEditorX, Expressions[i]->MaterialExpressionEditorY,
                *PredStr, *SuccStr, H,
                OrphanNodes.Contains(i) ? TEXT("ORPHAN") : TEXT(""));
        }

        return NumExprs;
    }

    int32 LayoutEdGraph(UEdGraph* Graph, bool bLeftToRight, float SpacingX, float SpacingY)
    {
        if (!Graph) return 0;
        TArray<UEdGraphNode*> AllNodes = Graph->Nodes;
        const int32 NumNodes = AllNodes.Num();
        if (NumNodes == 0) return 0;

        TArray<FLayoutNode> Nodes;
        Nodes.SetNum(NumNodes);
        TMap<UEdGraphNode*, int32> NodeToIndex;
        for (int32 i = 0; i < NumNodes; ++i)
        {
            Nodes[i].Id = i;
            NodeToIndex.Add(AllNodes[i], i);
        }

        for (int32 i = 0; i < NumNodes; ++i)
        {
            UEdGraphNode* Node = AllNodes[i];
            if (!Node) continue;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Input) continue;
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
                    int32* PredIdx = NodeToIndex.Find(LinkedPin->GetOwningNode());
                    if (PredIdx)
                    {
                        Nodes[i].Predecessors.AddUnique(*PredIdx);
                        Nodes[*PredIdx].Successors.AddUnique(i);
                    }
                }
            }
        }

        AssignLayers(Nodes);

        int32 MaxLayer = 0;
        for (const FLayoutNode& N : Nodes) MaxLayer = FMath::Max(MaxLayer, N.Layer);

        TMap<int32, TArray<int32>> Layers;
        for (const FLayoutNode& N : Nodes) Layers.FindOrAdd(N.Layer).Add(N.Id);

        for (auto& Pair : Layers)
        {
            Pair.Value.Sort([&AllNodes](int32 A, int32 B)
            {
                return AllNodes[A]->NodePosY < AllNodes[B]->NodePosY;
            });
        }

        for (auto& Pair : Layers)
        {
            int32 Layer = Pair.Key;
            const TArray<int32>& NodeIds = Pair.Value;
            float YOffset = -(NodeIds.Num() - 1) * SpacingY * 0.5f;

            for (int32 i = 0; i < NodeIds.Num(); ++i)
            {
                UEdGraphNode* Node = AllNodes[NodeIds[i]];
                if (!Node) continue;
                if (bLeftToRight)
                {
                    Node->NodePosX = static_cast<int32>((MaxLayer - Layer) * SpacingX);
                    Node->NodePosY = static_cast<int32>(YOffset + i * SpacingY);
                }
                else
                {
                    Node->NodePosX = static_cast<int32>(YOffset + i * SpacingX);
                    Node->NodePosY = static_cast<int32>((MaxLayer - Layer) * SpacingY);
                }
            }
        }
        return NumNodes;
    }
} // namespace GraphLayoutInternal

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleAutoLayoutGraph(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: asset_path"));
    }

    FString GraphName;
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    FString Direction = TEXT("left_to_right");
    Params->TryGetStringField(TEXT("direction"), Direction);
    bool bLeftToRight = (Direction != TEXT("top_to_bottom"));

    double SpacingXD = 400.0, SpacingYD = 200.0;
    Params->TryGetNumberField(TEXT("spacing_x"), SpacingXD);
    Params->TryGetNumberField(TEXT("spacing_y"), SpacingYD);
    float SpacingX = static_cast<float>(SpacingXD);
    float SpacingY = static_cast<float>(SpacingYD);

    UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
    if (!Asset)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    // Close editor to prevent save conflicts
    if (UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
    {
        IAssetEditorInstance* Editor = EditorSubsystem->FindEditorForAsset(Asset, false);
        if (Editor)
        {
            UE_LOG(LogSmithUE, Log, TEXT("auto_layout_graph: Closing editor for '%s'"), *AssetPath);
            EditorSubsystem->CloseAllEditorsForAsset(Asset);
        }
    }

    int32 TotalNodes = 0;
    FString AssetType;

    if (UMaterial* Mat = Cast<UMaterial>(Asset))
    {
        AssetType = TEXT("Material");
        TotalNodes = GraphLayoutInternal::LayoutMaterial(Mat, bLeftToRight, SpacingX, SpacingY);
        Mat->PreEditChange(nullptr);
        Mat->PostEditChange();
        Mat->MarkPackageDirty();
    }
    else if (UBlueprint* BP = Cast<UBlueprint>(Asset))
    {
        AssetType = TEXT("Blueprint");
        TArray<UEdGraph*> Graphs;
        for (UEdGraph* G : BP->UbergraphPages)
        {
            if (GraphName.IsEmpty() || G->GetName().Contains(GraphName))
                Graphs.Add(G);
        }
        for (UEdGraph* G : BP->FunctionGraphs)
        {
            if (GraphName.IsEmpty() || G->GetName().Contains(GraphName))
                Graphs.Add(G);
        }
        if (Graphs.Num() == 0)
        {
            return FSmithUECommonUtils::CreateErrorResponse(TEXT("No matching graphs found in Blueprint"));
        }
        for (UEdGraph* G : Graphs)
        {
            TotalNodes += GraphLayoutInternal::LayoutEdGraph(G, bLeftToRight, SpacingX, SpacingY);
        }
        BP->MarkPackageDirty();
    }
    else
    {
        AssetType = Asset->GetClass()->GetName();
        TArray<UEdGraph*> FoundGraphs;
        ForEachObjectWithOuter(Asset, [&FoundGraphs, &GraphName](UObject* Obj)
        {
            if (UEdGraph* G = Cast<UEdGraph>(Obj))
            {
                if (GraphName.IsEmpty() || G->GetName().Contains(GraphName))
                    FoundGraphs.Add(G);
            }
        }, true);

        if (FoundGraphs.Num() == 0)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("No graphs found in asset: %s (type: %s)"), *AssetPath, *AssetType));
        }
        for (UEdGraph* G : FoundGraphs)
        {
            TotalNodes += GraphLayoutInternal::LayoutEdGraph(G, bLeftToRight, SpacingX, SpacingY);
        }
        Asset->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("asset_type"), AssetType);
    Data->SetNumberField(TEXT("total_nodes"), TotalNodes);
    Data->SetStringField(TEXT("direction"), Direction);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
