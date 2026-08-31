// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEAssetFactoryCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"

#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColorAtlas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/DataAsset.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace SmithUEFactory
{
    static FString NormalizeAssetPath(const FString& Path, const FString& Name)
    {
        FString CleanPath = Path;
        CleanPath.RemoveFromEnd(TEXT("/"));
        if (Name.IsEmpty() || CleanPath.EndsWith(TEXT("/") + Name))
        {
            return CleanPath;
        }
        return CleanPath / Name;
    }

    static UPackage* MakeAssetPackage(const FString& Path, const FString& Name, FString& OutError)
    {
        const FString PackagePath = NormalizeAssetPath(Path, Name);
        if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
        {
            OutError = FString::Printf(TEXT("Asset already exists: %s"), *PackagePath);
            return nullptr;
        }
        UPackage* Package = CreatePackage(*PackagePath);
        if (!Package)
        {
            OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackagePath);
        }
        return Package;
    }

    static ETextureRenderTargetFormat ParseRTFormat(const FString& In)
    {
        const FString F = In.ToUpper();
        if (F == TEXT("R8"))      return RTF_R8;
        if (F == TEXT("RG8"))     return RTF_RG8;
        if (F == TEXT("RGBA8"))   return RTF_RGBA8;
        if (F == TEXT("R16F"))    return RTF_R16f;
        if (F == TEXT("RG16F"))   return RTF_RG16f;
        if (F == TEXT("RGBA16F")) return RTF_RGBA16f;
        if (F == TEXT("R32F"))    return RTF_R32f;
        if (F == TEXT("RG32F"))   return RTF_RG32f;
        if (F == TEXT("RGBA32F")) return RTF_RGBA32f;
        return RTF_RGBA8;
    }

    static FString RTFormatToString(ETextureRenderTargetFormat F)
    {
        switch (F)
        {
            case RTF_R8:       return TEXT("R8");
            case RTF_RG8:      return TEXT("RG8");
            case RTF_RGBA8:    return TEXT("RGBA8");
            case RTF_R16f:     return TEXT("R16f");
            case RTF_RG16f:    return TEXT("RG16f");
            case RTF_RGBA16f:  return TEXT("RGBA16f");
            case RTF_R32f:     return TEXT("R32f");
            case RTF_RG32f:    return TEXT("RG32f");
            case RTF_RGBA32f:  return TEXT("RGBA32f");
            default:           return TEXT("RGBA8");
        }
    }

    // Append the keys of a single FRichCurve as {time, value} objects under a named array.
    static void RichCurveToJson(const FRichCurve& Curve, const FString& Field, const TSharedPtr<FJsonObject>& Out)
    {
        TArray<TSharedPtr<FJsonValue>> Keys;
        for (const FRichCurveKey& K : Curve.GetConstRefOfKeys())
        {
            TSharedPtr<FJsonObject> KO = MakeShared<FJsonObject>();
            KO->SetNumberField(TEXT("time"), K.Time);
            KO->SetNumberField(TEXT("value"), K.Value);
            Keys.Add(MakeShared<FJsonValueObject>(KO));
        }
        Out->SetArrayField(Field, Keys);
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEAssetFactoryCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(TEXT("create_curve"), TEXT("Curve"),
            TEXT("Create a curve asset (Float, LinearColor, or Vector) with optional keyframes"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Curve asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path (e.g. /Game/Curves)"), true),
                FSmithUEToolParam(TEXT("curve_type"), TEXT("string"), TEXT("Float | LinearColor | Vector (default Float)")),
                FSmithUEToolParam(TEXT("keys"), TEXT("array"), TEXT("Optional keys. Float:[{time,value}] Vector:[{time,x,y,z}] LinearColor:[{time,r,g,b,a}]"))
            }),
        &HandleCreateCurve);

    Registry.Register(
        FSmithUEToolSchema(TEXT("read_curve"), TEXT("Curve"),
            TEXT("Read a curve's type and keyframes. Accepts a curve asset path, or a Blueprint-embedded Timeline curve sub-object path (e.g. /Game/Foo/BP_Bar.BP_Bar_C:CurveFloat_0, as reported by bp_get_summary timelines[].tracks[].curve)."),
            { FSmithUEToolParam(TEXT("curve_path"), TEXT("string"), TEXT("Curve asset path, or Package.Object:SubObject path for a Blueprint-embedded Timeline curve"), true) }),
        &HandleReadCurve);

    Registry.Register(
        FSmithUEToolSchema(TEXT("create_curve_atlas"), TEXT("Curve"),
            TEXT("Create a CurveLinearColorAtlas, optionally seeded with CurveLinearColor assets"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Atlas asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path"), true),
                FSmithUEToolParam(TEXT("width"), TEXT("int"), TEXT("Texture width (default 256)")),
                FSmithUEToolParam(TEXT("height"), TEXT("int"), TEXT("Texture height (default 256)")),
                FSmithUEToolParam(TEXT("curves"), TEXT("array"), TEXT("Optional array of CurveLinearColor asset paths to add as gradients"))
            }),
        &HandleCreateCurveAtlas);

    Registry.Register(
        FSmithUEToolSchema(TEXT("read_curve_atlas"), TEXT("Curve"),
            TEXT("Read a CurveLinearColorAtlas: texture size and gradient curve paths"),
            { FSmithUEToolParam(TEXT("atlas_path"), TEXT("string"), TEXT("Atlas asset path"), true) }),
        &HandleReadCurveAtlas);

    Registry.Register(
        FSmithUEToolSchema(TEXT("create_render_target"), TEXT("RenderTarget"),
            TEXT("Create a TextureRenderTarget2D"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Render target asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path"), true),
                FSmithUEToolParam(TEXT("width"), TEXT("int"), TEXT("Width in pixels (default 256)")),
                FSmithUEToolParam(TEXT("height"), TEXT("int"), TEXT("Height in pixels (default 256)")),
                FSmithUEToolParam(TEXT("format"), TEXT("string"), TEXT("RGBA8 | RGBA16f | RGBA32f | R8 | R16f | R32f | RG8 | RG16f | RG32f (default RGBA8)"))
            }),
        &HandleCreateRenderTarget);

    Registry.Register(
        FSmithUEToolSchema(TEXT("read_render_target"), TEXT("RenderTarget"),
            TEXT("Read a TextureRenderTarget2D: size and format"),
            { FSmithUEToolParam(TEXT("rt_path"), TEXT("string"), TEXT("Render target asset path"), true) }),
        &HandleReadRenderTarget);

    Registry.Register(
        FSmithUEToolSchema(TEXT("create_data_asset"), TEXT("Data"),
            TEXT("Create a Data Asset from a CONCRETE, non-abstract UDataAsset subclass (abstract bases like UDataAsset/UPrimaryDataAsset are rejected)."),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Data asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path"), true),
                FSmithUEToolParam(TEXT("class_path"), TEXT("string"), TEXT("Concrete UDataAsset subclass, required (e.g. /Game/BP_MyData.BP_MyData_C or /Script/MyGame.MyData). UDataAsset/UPrimaryDataAsset bases are abstract."), true)
            }),
        &HandleCreateDataAsset);

    Registry.Register(
        FSmithUEToolSchema(TEXT("read_data_asset"), TEXT("Data"),
            TEXT("Read a Data Asset's class and all UPROPERTY values via reflection"),
            { FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Data asset path"), true) }),
        &HandleReadDataAsset);

    Registry.Register(
        FSmithUEToolSchema(TEXT("create_physical_material"), TEXT("Physics"),
            TEXT("Create a PhysicalMaterial with friction/restitution/density"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Physical material asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path"), true),
                FSmithUEToolParam(TEXT("friction"), TEXT("number"), TEXT("Surface friction (default 0.7)")),
                FSmithUEToolParam(TEXT("restitution"), TEXT("number"), TEXT("Bounciness 0..1 (default 0.3)")),
                FSmithUEToolParam(TEXT("density"), TEXT("number"), TEXT("Density g/cm^3 (default 1.0)"))
            }),
        &HandleCreatePhysicalMaterial);

    Registry.Register(
        FSmithUEToolSchema(TEXT("read_physical_material"), TEXT("Physics"),
            TEXT("Read a PhysicalMaterial's friction/restitution/density"),
            { FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Physical material asset path"), true) }),
        &HandleReadPhysicalMaterial);
}

// ---------------------------------------------------------------------------
// Curve
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleCreateCurve(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name"), TEXT("path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString Name, Path, CurveType = TEXT("Float");
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("curve_type"), CurveType);

    const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
    Params->TryGetArrayField(TEXT("keys"), Keys);

    UPackage* Package = SmithUEFactory::MakeAssetPackage(Path, Name, Error);
    if (!Package) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

    int32 KeyCount = 0;
    UObject* NewAsset = nullptr;
    const FString T = CurveType.ToLower();

    if (T == TEXT("linearcolor") || T == TEXT("color"))
    {
        UCurveLinearColor* C = NewObject<UCurveLinearColor>(Package, FName(*Name), RF_Public | RF_Standalone);
        if (Keys)
        {
            for (const TSharedPtr<FJsonValue>& KV : *Keys)
            {
                const TSharedPtr<FJsonObject> KO = KV->AsObject();
                if (!KO) continue;
                double Time = 0, R = 0, G = 0, B = 0, A = 1;
                KO->TryGetNumberField(TEXT("time"), Time);
                KO->TryGetNumberField(TEXT("r"), R); KO->TryGetNumberField(TEXT("g"), G);
                KO->TryGetNumberField(TEXT("b"), B); KO->TryGetNumberField(TEXT("a"), A);
                C->FloatCurves[0].AddKey(Time, R); C->FloatCurves[1].AddKey(Time, G);
                C->FloatCurves[2].AddKey(Time, B); C->FloatCurves[3].AddKey(Time, A);
                KeyCount++;
            }
        }
        NewAsset = C;
    }
    else if (T == TEXT("vector"))
    {
        UCurveVector* C = NewObject<UCurveVector>(Package, FName(*Name), RF_Public | RF_Standalone);
        if (Keys)
        {
            for (const TSharedPtr<FJsonValue>& KV : *Keys)
            {
                const TSharedPtr<FJsonObject> KO = KV->AsObject();
                if (!KO) continue;
                double Time = 0, X = 0, Y = 0, Z = 0;
                KO->TryGetNumberField(TEXT("time"), Time);
                KO->TryGetNumberField(TEXT("x"), X); KO->TryGetNumberField(TEXT("y"), Y);
                KO->TryGetNumberField(TEXT("z"), Z);
                C->FloatCurves[0].AddKey(Time, X); C->FloatCurves[1].AddKey(Time, Y);
                C->FloatCurves[2].AddKey(Time, Z);
                KeyCount++;
            }
        }
        NewAsset = C;
    }
    else
    {
        UCurveFloat* C = NewObject<UCurveFloat>(Package, FName(*Name), RF_Public | RF_Standalone);
        if (Keys)
        {
            for (const TSharedPtr<FJsonValue>& KV : *Keys)
            {
                const TSharedPtr<FJsonObject> KO = KV->AsObject();
                if (!KO) continue;
                double Time = 0, Value = 0;
                KO->TryGetNumberField(TEXT("time"), Time);
                KO->TryGetNumberField(TEXT("value"), Value);
                C->FloatCurve.AddKey(Time, Value);
                KeyCount++;
            }
        }
        NewAsset = C;
    }

    FAssetRegistryModule::AssetCreated(NewAsset);
    NewAsset->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(NewAsset);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), NewAsset->GetName());
    Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
    Data->SetStringField(TEXT("curve_type"), CurveType);
    Data->SetNumberField(TEXT("key_count"), KeyCount);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleReadCurve(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("curve_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    FString CurvePath;
    Params->TryGetStringField(TEXT("curve_path"), CurvePath);

    UObject* Asset = UEditorAssetLibrary::LoadAsset(CurvePath);
    if (!Asset)
    {
        // Timeline curves live INSIDE a Blueprint
        // (/Game/Foo/BP_Bar.BP_Bar_C:CurveFloat_0) and are not assets, so
        // LoadAsset cannot see them -- resolve the sub-object directly.
        Asset = LoadObject<UCurveBase>(nullptr, *CurvePath);
    }
    if (!Asset) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Curve not found: %s"), *CurvePath)); }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

    if (UCurveLinearColor* CLin = Cast<UCurveLinearColor>(Asset))
    {
        Data->SetStringField(TEXT("curve_type"), TEXT("LinearColor"));
        SmithUEFactory::RichCurveToJson(CLin->FloatCurves[0], TEXT("r"), Data);
        SmithUEFactory::RichCurveToJson(CLin->FloatCurves[1], TEXT("g"), Data);
        SmithUEFactory::RichCurveToJson(CLin->FloatCurves[2], TEXT("b"), Data);
        SmithUEFactory::RichCurveToJson(CLin->FloatCurves[3], TEXT("a"), Data);
    }
    else if (UCurveVector* CVec = Cast<UCurveVector>(Asset))
    {
        Data->SetStringField(TEXT("curve_type"), TEXT("Vector"));
        SmithUEFactory::RichCurveToJson(CVec->FloatCurves[0], TEXT("x"), Data);
        SmithUEFactory::RichCurveToJson(CVec->FloatCurves[1], TEXT("y"), Data);
        SmithUEFactory::RichCurveToJson(CVec->FloatCurves[2], TEXT("z"), Data);
    }
    else if (UCurveFloat* CFloat = Cast<UCurveFloat>(Asset))
    {
        Data->SetStringField(TEXT("curve_type"), TEXT("Float"));
        SmithUEFactory::RichCurveToJson(CFloat->FloatCurve, TEXT("keys"), Data);
    }
    else
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset is not a curve: %s"), *CurvePath));
    }

    Data->SetStringField(TEXT("path"), CurvePath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Curve Linear Color Atlas
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleCreateCurveAtlas(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name"), TEXT("path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    FString Name, Path;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);

    int32 Width = 256, Height = 256;
    Params->TryGetNumberField(TEXT("width"), Width);
    Params->TryGetNumberField(TEXT("height"), Height);

    UPackage* Package = SmithUEFactory::MakeAssetPackage(Path, Name, Error);
    if (!Package) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

    UCurveLinearColorAtlas* Atlas = NewObject<UCurveLinearColorAtlas>(Package, FName(*Name), RF_Public | RF_Standalone);
    Atlas->TextureSize = FMath::Max(1, Width);

    int32 CurveCount = 0;
    const TArray<TSharedPtr<FJsonValue>>* Curves = nullptr;
    if (Params->TryGetArrayField(TEXT("curves"), Curves) && Curves)
    {
        for (const TSharedPtr<FJsonValue>& CV : *Curves)
        {
            const FString CurvePath = CV->AsString();
            if (CurvePath.IsEmpty()) continue;
            if (UCurveLinearColor* LC = Cast<UCurveLinearColor>(UEditorAssetLibrary::LoadAsset(CurvePath)))
            {
                Atlas->GradientCurves.Add(LC);
                CurveCount++;
            }
        }
    }

    FAssetRegistryModule::AssetCreated(Atlas);
    Atlas->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(Atlas);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Atlas->GetName());
    Data->SetStringField(TEXT("path"), Atlas->GetPathName());
    Data->SetNumberField(TEXT("width"), Width);
    Data->SetNumberField(TEXT("height"), Height);
    Data->SetNumberField(TEXT("curve_count"), CurveCount);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleReadCurveAtlas(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("atlas_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    FString AtlasPath;
    Params->TryGetStringField(TEXT("atlas_path"), AtlasPath);

    UCurveLinearColorAtlas* Atlas = Cast<UCurveLinearColorAtlas>(UEditorAssetLibrary::LoadAsset(AtlasPath));
    if (!Atlas) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("CurveLinearColorAtlas not found: %s"), *AtlasPath)); }

    TArray<TSharedPtr<FJsonValue>> CurvePaths;
    for (const UCurveLinearColor* LC : Atlas->GradientCurves)
    {
        if (LC) { CurvePaths.Add(MakeShared<FJsonValueString>(LC->GetPathName())); }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("path"), AtlasPath);
    Data->SetNumberField(TEXT("width"), Atlas->TextureSize);
    Data->SetNumberField(TEXT("height"), Atlas->TextureSize);
    Data->SetArrayField(TEXT("curves"), CurvePaths);
    Data->SetNumberField(TEXT("curve_count"), CurvePaths.Num());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Texture Render Target 2D
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleCreateRenderTarget(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name"), TEXT("path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    FString Name, Path, Format = TEXT("RGBA8");
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("format"), Format);

    int32 Width = 256, Height = 256;
    Params->TryGetNumberField(TEXT("width"), Width);
    Params->TryGetNumberField(TEXT("height"), Height);

    UPackage* Package = SmithUEFactory::MakeAssetPackage(Path, Name, Error);
    if (!Package) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

    UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Package, FName(*Name), RF_Public | RF_Standalone);
    RT->RenderTargetFormat = SmithUEFactory::ParseRTFormat(Format);
    RT->ClearColor = FLinearColor::Black;
    RT->InitAutoFormat(FMath::Max(1, Width), FMath::Max(1, Height));
    RT->UpdateResourceImmediate(true);

    FAssetRegistryModule::AssetCreated(RT);
    RT->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(RT);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), RT->GetName());
    Data->SetStringField(TEXT("path"), RT->GetPathName());
    Data->SetNumberField(TEXT("width"), RT->SizeX);
    Data->SetNumberField(TEXT("height"), RT->SizeY);
    Data->SetStringField(TEXT("format"), SmithUEFactory::RTFormatToString(RT->RenderTargetFormat));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleReadRenderTarget(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("rt_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    FString RTPath;
    Params->TryGetStringField(TEXT("rt_path"), RTPath);

    UTextureRenderTarget2D* RT = Cast<UTextureRenderTarget2D>(UEditorAssetLibrary::LoadAsset(RTPath));
    if (!RT) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Render target not found: %s"), *RTPath)); }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("path"), RTPath);
    Data->SetNumberField(TEXT("width"), RT->SizeX);
    Data->SetNumberField(TEXT("height"), RT->SizeY);
    Data->SetStringField(TEXT("format"), SmithUEFactory::RTFormatToString(RT->RenderTargetFormat));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Data Asset
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleCreateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name"), TEXT("path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    FString Name, Path, ClassPath;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("class_path"), ClassPath);

    if (ClassPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            TEXT("class_path is required: UDataAsset/UPrimaryDataAsset are abstract. Provide a concrete subclass (e.g. /Game/BP_MyData.BP_MyData_C or /Script/MyGame.MyData)."));
    }
    UClass* AssetClass = LoadClass<UDataAsset>(nullptr, *ClassPath);
    if (!AssetClass) { AssetClass = LoadObject<UClass>(nullptr, *ClassPath); }
    if (!AssetClass || !AssetClass->IsChildOf(UDataAsset::StaticClass()))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("class_path is not a UDataAsset subclass: %s"), *ClassPath));
    }

    UPackage* Package = SmithUEFactory::MakeAssetPackage(Path, Name, Error);
    if (!Package) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

    if (AssetClass->HasAnyClassFlags(CLASS_Abstract))
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Cannot instantiate abstract class %s; provide a concrete UDataAsset subclass via class_path"), *AssetClass->GetName()));
    }

    UDataAsset* NewAsset = NewObject<UDataAsset>(Package, AssetClass, FName(*Name), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(NewAsset);
    NewAsset->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(NewAsset);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), NewAsset->GetName());
    Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
    Data->SetStringField(TEXT("class"), AssetClass->GetName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    UDataAsset* Asset = Cast<UDataAsset>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!Asset) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Data asset not found: %s"), *AssetPath)); }

    TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
    FJsonObjectConverter::UStructToJsonObject(Asset->GetClass(), Asset, Props.ToSharedRef(), 0, 0);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("path"), AssetPath);
    Data->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
    Data->SetObjectField(TEXT("properties"), Props);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Physical Material
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleCreatePhysicalMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name"), TEXT("path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    FString Name, Path;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);

    double Friction = 0.7, Restitution = 0.3, Density = 1.0;
    Params->TryGetNumberField(TEXT("friction"), Friction);
    Params->TryGetNumberField(TEXT("restitution"), Restitution);
    Params->TryGetNumberField(TEXT("density"), Density);

    UPackage* Package = SmithUEFactory::MakeAssetPackage(Path, Name, Error);
    if (!Package) { return FSmithUECommonUtils::CreateErrorResponse(Error); }

    UPhysicalMaterial* PM = NewObject<UPhysicalMaterial>(Package, FName(*Name), RF_Public | RF_Standalone);
    PM->Friction = Friction;
    PM->Restitution = Restitution;
    PM->Density = Density;

    FAssetRegistryModule::AssetCreated(PM);
    PM->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(PM);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), PM->GetName());
    Data->SetStringField(TEXT("path"), PM->GetPathName());
    Data->SetNumberField(TEXT("friction"), PM->Friction);
    Data->SetNumberField(TEXT("restitution"), PM->Restitution);
    Data->SetNumberField(TEXT("density"), PM->Density);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAssetFactoryCommands::HandleReadPhysicalMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    UPhysicalMaterial* PM = Cast<UPhysicalMaterial>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!PM) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Physical material not found: %s"), *AssetPath)); }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("path"), AssetPath);
    Data->SetNumberField(TEXT("friction"), PM->Friction);
    Data->SetNumberField(TEXT("restitution"), PM->Restitution);
    Data->SetNumberField(TEXT("density"), PM->Density);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
