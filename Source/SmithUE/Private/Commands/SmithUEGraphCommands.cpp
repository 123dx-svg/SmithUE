// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEGraphCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"
#include "EditorAssetLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Engine/Blueprint.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "Blueprint/SmithUEBpAtomicAPI.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "SMyBlueprint.h"
#include "Types/SlateEnums.h"

// ---------------------------------------------------------------------------
// Core layout algorithm (works on abstract adjacency)
// ---------------------------------------------------------------------------

namespace
{
    struct FLayoutNode
    {
        int32 Id;
        int32 Layer = -1;
        TArray<int32> Predecessors; // nodes feeding into this node
        TArray<int32> Successors;   // nodes this node feeds into
    };

    /**
     * Assign layers via longest-path from sinks (output nodes).
     * Sink nodes (no successors) get layer 0, predecessors get layer+1.
     */
    void AssignLayers(TArray<FLayoutNode>& Nodes)
    {
        // Find sinks
        TQueue<int32> Queue;
        for (FLayoutNode& N : Nodes)
        {
            if (N.Successors.Num() == 0)
            {
                N.Layer = 0;
                Queue.Enqueue(N.Id);
            }
        }

        // If no sinks (all cyclic), start from first node
        if (Queue.IsEmpty())
        {
            Nodes[0].Layer = 0;
            Queue.Enqueue(0);
        }

        // BFS backward
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

        // Handle orphans (never reached)
        int32 MaxLayer = 0;
        for (const FLayoutNode& N : Nodes)
        {
            if (N.Layer >= 0)
                MaxLayer = FMath::Max(MaxLayer, N.Layer);
        }
        for (FLayoutNode& N : Nodes)
        {
            if (N.Layer < 0)
                N.Layer = MaxLayer + 1;
        }
    }

    // ---------------------------------------------------------------------------
    // Material-specific layout (works directly on UMaterialExpression)
    // ---------------------------------------------------------------------------

    int32 LayoutMaterial(UMaterial* Material, bool bLeftToRight, float SpacingX, float SpacingY)
    {
        const auto Expressions = Material->GetExpressions();
        const int32 NumExprs = Expressions.Num();
        if (NumExprs == 0) return 0;

        // Build layout nodes from expression connections
        TArray<FLayoutNode> Nodes;
        Nodes.SetNum(NumExprs);
        for (int32 i = 0; i < NumExprs; ++i)
        {
            Nodes[i].Id = i;
        }

        // Map expression ptr -> index
        TMap<UMaterialExpression*, int32> ExprToIndex;
        for (int32 i = 0; i < NumExprs; ++i)
        {
            if (Expressions[i])
                ExprToIndex.Add(Expressions[i], i);
        }

        // Build adjacency from expression inputs
        for (int32 i = 0; i < NumExprs; ++i)
        {
            UMaterialExpression* Expr = Expressions[i];
            if (!Expr) continue;

            // Get all inputs of this expression
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

        // Also consider material output connections (expressions connected to material result)
        // These are the "true sinks" — mark them by checking which expressions have no successors
        // that connect to the material base inputs
        auto* EditorData = Material->GetEditorOnlyData();
        if (EditorData)
        {
            auto CheckMaterialInput = [&](FExpressionInput& MatInput)
            {
                if (MatInput.Expression)
                {
                    int32* Idx = ExprToIndex.Find(MatInput.Expression);
                    if (Idx)
                    {
                        // This expression feeds into material output — it's a sink
                        // (its Successors list stays empty or we just don't add anything)
                    }
                }
            };
            CheckMaterialInput(EditorData->BaseColor);
            CheckMaterialInput(EditorData->Metallic);
            CheckMaterialInput(EditorData->Roughness);
            CheckMaterialInput(EditorData->Normal);
            CheckMaterialInput(EditorData->EmissiveColor);
            CheckMaterialInput(EditorData->Opacity);
            CheckMaterialInput(EditorData->OpacityMask);
        }

        // Assign layers
        AssignLayers(Nodes);

        // Find max layer for coordinate calculation
        int32 MaxLayer = 0;
        for (const FLayoutNode& N : Nodes)
        {
            MaxLayer = FMath::Max(MaxLayer, N.Layer);
        }

        // Group by layer and sort within layer by original Y for stability
        TMap<int32, TArray<int32>> Layers;
        for (const FLayoutNode& N : Nodes)
        {
            Layers.FindOrAdd(N.Layer).Add(N.Id);
        }

        // Sort within each layer by current Y position
        for (auto& Pair : Layers)
        {
            Pair.Value.Sort([&Expressions](int32 A, int32 B)
            {
                float YA = Expressions[A] ? Expressions[A]->MaterialExpressionEditorY : 0.f;
                float YB = Expressions[B] ? Expressions[B]->MaterialExpressionEditorY : 0.f;
                return YA < YB;
            });
        }

        // Assign positions
        for (auto& Pair : Layers)
        {
            int32 Layer = Pair.Key;
            const TArray<int32>& NodeIds = Pair.Value;

            // Center vertically
            float YOffset = -(NodeIds.Num() - 1) * SpacingY * 0.5f;

            for (int32 i = 0; i < NodeIds.Num(); ++i)
            {
                UMaterialExpression* Expr = Expressions[NodeIds[i]];
                if (!Expr) continue;

                if (bLeftToRight)
                {
                    // Layer 0 = rightmost (output/sink), MaxLayer = leftmost (source/inputs)
                    Expr->MaterialExpressionEditorX = static_cast<int32>((MaxLayer - Layer) * SpacingX);
                    Expr->MaterialExpressionEditorY = static_cast<int32>(YOffset + i * SpacingY);
                }
                else
                {
                    Expr->MaterialExpressionEditorX = static_cast<int32>(YOffset + i * SpacingX);
                    Expr->MaterialExpressionEditorY = static_cast<int32>((MaxLayer - Layer) * SpacingY);
                }
            }
        }

        return NumExprs;
    }

    // ---------------------------------------------------------------------------
    // Generic UEdGraph layout (Blueprint, Niagara, etc.)
    // ---------------------------------------------------------------------------

    int32 LayoutEdGraph(UEdGraph* Graph, bool bLeftToRight, float SpacingX, float SpacingY)
    {
        if (!Graph) return 0;

        TArray<UEdGraphNode*> AllNodes = Graph->Nodes;
        const int32 NumNodes = AllNodes.Num();
        if (NumNodes == 0) return 0;

        // Build layout nodes
        TArray<FLayoutNode> Nodes;
        Nodes.SetNum(NumNodes);

        TMap<UEdGraphNode*, int32> NodeToIndex;
        for (int32 i = 0; i < NumNodes; ++i)
        {
            Nodes[i].Id = i;
            NodeToIndex.Add(AllNodes[i], i);
        }

        // Build adjacency from pins
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

        // Assign layers
        AssignLayers(Nodes);

        int32 MaxLayer = 0;
        for (const FLayoutNode& N : Nodes)
        {
            MaxLayer = FMath::Max(MaxLayer, N.Layer);
        }

        // Group by layer
        TMap<int32, TArray<int32>> Layers;
        for (const FLayoutNode& N : Nodes)
        {
            Layers.FindOrAdd(N.Layer).Add(N.Id);
        }

        // Sort within layer by original Y
        for (auto& Pair : Layers)
        {
            Pair.Value.Sort([&AllNodes](int32 A, int32 B)
            {
                return AllNodes[A]->NodePosY < AllNodes[B]->NodePosY;
            });
        }

        // Assign positions
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

    // ---------------------------------------------------------------------------
    // Find UEdGraphs for Blueprint assets
    // ---------------------------------------------------------------------------

    TArray<UEdGraph*> FindBlueprintGraphs(UBlueprint* BP, const FString& GraphNameFilter)
    {
        TArray<UEdGraph*> Results;
        if (!BP) return Results;

        auto AddFiltered = [&](UEdGraph* G)
        {
            if (!G) return;
            if (GraphNameFilter.IsEmpty() || G->GetName().Contains(GraphNameFilter))
            {
                Results.Add(G);
            }
        };

        for (UEdGraph* G : BP->UbergraphPages) AddFiltered(G);
        for (UEdGraph* G : BP->FunctionGraphs) AddFiltered(G);
        for (UEdGraph* G : BP->MacroGraphs) AddFiltered(G);

        return Results;
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEGraphCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("auto_layout_graph"),
            TEXT("Editor"),
            TEXT("Auto-arrange nodes in any graph (Material, Blueprint, Niagara). Closes editor if open to prevent save conflicts."),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Full asset path (e.g. /Game/Materials/M_Test or /Game/BP/BP_Actor)"), true),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name filter (e.g. 'EventGraph'). For materials, omit.")),
                FSmithUEToolParam(TEXT("direction"), TEXT("string"), TEXT("Layout direction: 'left_to_right' (default) or 'top_to_bottom'")),
                FSmithUEToolParam(TEXT("spacing_x"), TEXT("number"), TEXT("Horizontal spacing between layers (default: 400)")),
                FSmithUEToolParam(TEXT("spacing_y"), TEXT("number"), TEXT("Vertical spacing between nodes in same layer (default: 200)"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEGraphCommands::HandleAutoLayoutGraph(Params);
        });

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_focus_node"),
            TEXT("Editor"),
            TEXT("Open a Blueprint editor and focus a node (node_id+graph_name), function graph (function_name), or variable (variable_name). Provide exactly ONE target: node_id (+graph_name) | function_name | variable_name."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name (required with node_id)")),
                FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("Node GUID to focus")),
                FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("Function graph to open")),
                FSmithUEToolParam(TEXT("variable_name"), TEXT("string"), TEXT("Variable to select in My Blueprint"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEGraphCommands::HandleFocusNode(Params);
        });
}

// ---------------------------------------------------------------------------
// HandleAutoLayoutGraph
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEGraphCommands::HandleAutoLayoutGraph(const TSharedPtr<FJsonObject>& Params)
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

    // Load asset
    UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
    if (!Asset)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    // Close editor to prevent save conflicts (same pattern as LoadMaterialForWriting)
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

    // --- Material: expression-level layout ---
    if (UMaterial* Mat = Cast<UMaterial>(Asset))
    {
        AssetType = TEXT("Material");
        TotalNodes = LayoutMaterial(Mat, bLeftToRight, SpacingX, SpacingY);

        Mat->PreEditChange(nullptr);
        Mat->PostEditChange();
        Mat->MarkPackageDirty();
    }
    // --- Blueprint: UEdGraph layout ---
    else if (UBlueprint* BP = Cast<UBlueprint>(Asset))
    {
        AssetType = TEXT("Blueprint");
        TArray<UEdGraph*> Graphs = FindBlueprintGraphs(BP, GraphName);
        if (Graphs.Num() == 0)
        {
            return FSmithUECommonUtils::CreateErrorResponse(TEXT("No matching graphs found in Blueprint"));
        }
        for (UEdGraph* G : Graphs)
        {
            TotalNodes += LayoutEdGraph(G, bLeftToRight, SpacingX, SpacingY);
        }
        BP->MarkPackageDirty();
    }
    // --- Fallback: try to find UEdGraph in any asset ---
    else
    {
        AssetType = TEXT("Unknown");
        // Search for UEdGraph subobjects
        TArray<UEdGraph*> FoundGraphs;
        ForEachObjectWithOuter(Asset, [&FoundGraphs, &GraphName](UObject* Obj)
        {
            if (UEdGraph* G = Cast<UEdGraph>(Obj))
            {
                if (GraphName.IsEmpty() || G->GetName().Contains(GraphName))
                {
                    FoundGraphs.Add(G);
                }
            }
        }, true);

        if (FoundGraphs.Num() == 0)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("No graphs found in asset: %s (type: %s)"), *AssetPath, *Asset->GetClass()->GetName()));
        }

        for (UEdGraph* G : FoundGraphs)
        {
            TotalNodes += LayoutEdGraph(G, bLeftToRight, SpacingX, SpacingY);
        }
        Asset->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("asset_type"), AssetType);
    Data->SetNumberField(TEXT("total_nodes_laid_out"), TotalNodes);
    Data->SetStringField(TEXT("direction"), Direction);
    Data->SetNumberField(TEXT("spacing_x"), SpacingX);
    Data->SetNumberField(TEXT("spacing_y"), SpacingY);

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleFocusNode
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEGraphCommands::HandleFocusNode(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    // -----------------------------------------------------------------------
    // 1. Validate bp_path
    // -----------------------------------------------------------------------
    FString BpPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BpPath) || BpPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: bp_path"));
    }

    // -----------------------------------------------------------------------
    // 2. Extract optional focus-mode params
    // -----------------------------------------------------------------------
    FString NodeId, GraphName, FunctionName, VariableName;
    Params->TryGetStringField(TEXT("node_id"),       NodeId);
    Params->TryGetStringField(TEXT("graph_name"),    GraphName);
    Params->TryGetStringField(TEXT("function_name"), FunctionName);
    Params->TryGetStringField(TEXT("variable_name"), VariableName);

    // -----------------------------------------------------------------------
    // 3. Validate: exactly one of (node_id, function_name, variable_name)
    // -----------------------------------------------------------------------
    const int32 ModeCount = (NodeId.IsEmpty()       ? 0 : 1)
                          + (FunctionName.IsEmpty()  ? 0 : 1)
                          + (VariableName.IsEmpty()  ? 0 : 1);

    if (ModeCount == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            TEXT("Exactly one of node_id, function_name, or variable_name must be specified"));
    }
    if (ModeCount > 1)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            TEXT("Only one of node_id, function_name, or variable_name may be specified at a time"));
    }
    if (!NodeId.IsEmpty() && GraphName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            TEXT("graph_name is required when node_id is specified"));
    }

    // -----------------------------------------------------------------------
    // 4. Load Blueprint (reuses FSmithUEBpAtomicAPI::LoadBlueprint pattern,
    //    which handles level:current / level:/Game/... prefixes and path
    //    normalisation identically to SmithUEBlueprintCommands.cpp)
    // -----------------------------------------------------------------------
    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BpPath));
    }

    // -----------------------------------------------------------------------
    // 5. Open editor + get IBlueprintEditor interface
    //    (game-thread safety: HTTP server's IsGameThreadRequired() returns true
    //    for all non-worker-safe POST /api/v1/execute commands, so the entire
    //    RouteRequest is already dispatched via AsyncTask(GameThread,...).
    //    This is the same mechanism used by auto_layout_graph.)
    // -----------------------------------------------------------------------
    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor not available"));
    }

    UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!EditorSubsystem)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("AssetEditorSubsystem not available"));
    }

    // Ensure the editor window is open before asking for the interface.
    EditorSubsystem->OpenEditorForAsset(BP);

    TSharedPtr<IBlueprintEditor> BPEditorIface =
        FKismetEditorUtilities::GetIBlueprintEditorForObject(BP, /*bOpenEditor=*/true);
    if (!BPEditorIface.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to open Blueprint editor"));
    }

    // =======================================================================
    // Mode A: Focus a specific node by NodeGuid + graph_name
    // =======================================================================
    if (!NodeId.IsEmpty())
    {
        // Search all graph collections for the named graph (case-insensitive,
        // mirroring FindSmithUEGraphByName in SmithUEBlueprintCommands.cpp)
        UEdGraph* TargetGraph = nullptr;
        auto SearchGraphCollection = [&GraphName](const TArray<UEdGraph*>& Graphs) -> UEdGraph*
        {
            for (UEdGraph* G : Graphs)
            {
                if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
                {
                    return G;
                }
            }
            return nullptr;
        };

        if (!TargetGraph) { TargetGraph = SearchGraphCollection(BP->UbergraphPages);  }
        if (!TargetGraph) { TargetGraph = SearchGraphCollection(BP->FunctionGraphs);  }
        if (!TargetGraph) { TargetGraph = SearchGraphCollection(BP->MacroGraphs);     }

        if (!TargetGraph)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Graph not found: %s"), *GraphName));
        }

        // Resolve the GUID (mirrors FindTraceNode GUID branch in SmithUEBlueprintCommands.cpp)
        FGuid NodeGuid;
        if (!FGuid::Parse(NodeId, NodeGuid))
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Invalid node GUID format: %s"), *NodeId));
        }

        UEdGraphNode* TargetNode = nullptr;
        for (UEdGraphNode* Node : TargetGraph->Nodes)
        {
            if (Node && Node->NodeGuid == NodeGuid)
            {
                TargetNode = Node;
                break;
            }
        }

        if (!TargetNode)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Node '%s' not found in graph '%s'"), *NodeId, *GraphName));
        }

        // JumpToHyperlink centers + highlights the node in the graph viewport.
        BPEditorIface->JumpToHyperlink(TargetNode, /*bRequestRename=*/false);

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("focused"),     TEXT("node"));
        Data->SetStringField(TEXT("target"),      NodeId);
        Data->SetStringField(TEXT("graph"),       GraphName);
        Data->SetStringField(TEXT("node_title"),
            TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }

    // =======================================================================
    // Mode B: Open a function graph and bring it to front
    // =======================================================================
    if (!FunctionName.IsEmpty())
    {
        UEdGraph* FunctionGraph = nullptr;

        // Primary: BP->FunctionGraphs (named user functions)
        for (UEdGraph* G : BP->FunctionGraphs)
        {
            if (G && G->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
            {
                FunctionGraph = G;
                break;
            }
        }
        // Fallback: ubergraph pages (e.g. "EventGraph" requested by name)
        if (!FunctionGraph)
        {
            for (UEdGraph* G : BP->UbergraphPages)
            {
                if (G && G->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
                {
                    FunctionGraph = G;
                    break;
                }
            }
        }
        // Fallback: macro graphs
        if (!FunctionGraph)
        {
            for (UEdGraph* G : BP->MacroGraphs)
            {
                if (G && G->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
                {
                    FunctionGraph = G;
                    break;
                }
            }
        }

        if (!FunctionGraph)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
        }

        // OpenGraphAndBringToFront switches the active tab and focuses the graph.
        BPEditorIface->OpenGraphAndBringToFront(FunctionGraph, /*bSetFocus=*/true);

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("focused"), TEXT("function"));
        Data->SetStringField(TEXT("target"),  FunctionName);
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }

    // =======================================================================
    // Mode C: Select a variable in the My Blueprint panel
    // =======================================================================
    if (!VariableName.IsEmpty())
    {
        // IBlueprintEditor is implemented by FBlueprintEditor; the cast is
        // safe because OpenEditorForAsset / GetIBlueprintEditorForObject always
        // returns a FBlueprintEditor for UBlueprint assets.
        TSharedPtr<FBlueprintEditor> BPEditor =
            StaticCastSharedPtr<FBlueprintEditor>(BPEditorIface);
        if (!BPEditor.IsValid())
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                TEXT("Failed to get concrete FBlueprintEditor (StaticCast failed)"));
        }

        TSharedPtr<SMyBlueprint> MyBPWidget = BPEditor->GetMyBlueprintWidget();
        if (!MyBPWidget.IsValid())
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                TEXT("My Blueprint panel not available — ensure editor is fully open"));
        }

        // SelectItemByName scrolls to and highlights the variable row.
        // NodeSectionID::VARIABLE targets the Variables section; use
        // NodeSectionID::FUNCTION for the Functions section instead.
        MyBPWidget->SelectItemByName(
            FName(*VariableName),
            ESelectInfo::Direct,
            NodeSectionID::VARIABLE,
            /*bIsCategory=*/false);

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("focused"), TEXT("variable"));
        Data->SetStringField(TEXT("target"),  VariableName);
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }

    // Should be unreachable — ModeCount validation above guarantees one mode.
    return FSmithUECommonUtils::CreateErrorResponse(TEXT("Internal error: no focus mode executed"));

#else
    return FSmithUECommonUtils::CreateErrorResponse(TEXT("bp_focus_node requires WITH_EDITOR"));
#endif
}
