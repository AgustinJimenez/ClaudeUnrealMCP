// Sprint 27 — Animation Authoring
#include "MCPServer.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimationGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_VariableGet.h"
#include "EditorAnimUtils.h"
#include "Animation/AnimationAsset.h"
#include "AnimPose.h"

// ===== MONTAGE =====

FString FMCPServer::HandleCreateMontage(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString AssetPath, AssetName, SkeletonPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return MakeError(TEXT("asset_path required"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return MakeError(TEXT("asset_name required"));
	if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
		return MakeError(TEXT("skeleton_path required (path to USkeleton asset)"));

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
		return MakeError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = Skeleton;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, AssetPath, UAnimMontage::StaticClass(), Factory);
	if (!NewAsset)
		return MakeError(TEXT("Failed to create AnimMontage"));

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName(), false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Data->SetStringField(TEXT("skeleton"), SkeletonPath);
	return MakeResponse(true, Data);
}

FString FMCPServer::HandleAddMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString MontagePath, SectionName;
	if (!Params->TryGetStringField(TEXT("montage_path"), MontagePath))
		return MakeError(TEXT("montage_path required"));
	if (!Params->TryGetStringField(TEXT("section_name"), SectionName))
		return MakeError(TEXT("section_name required"));

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
	if (!Montage)
		return MakeError(FString::Printf(TEXT("Montage not found: %s"), *MontagePath));

	// Check if section exists
	int32 ExistingIdx = Montage->GetSectionIndex(FName(*SectionName));
	if (ExistingIdx != INDEX_NONE)
		return MakeError(FString::Printf(TEXT("Section already exists: %s (index %d)"), *SectionName, ExistingIdx));

	// Add section
	FCompositeSection NewSection;
	NewSection.SectionName = FName(*SectionName);

	double StartTime = 0;
	Params->TryGetNumberField(TEXT("start_time"), StartTime);
	NewSection.SetTime(static_cast<float>(StartTime));
	Montage->CompositeSections.Add(NewSection);
	Montage->Modify();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("section"), SectionName);
	Data->SetNumberField(TEXT("start_time"), StartTime);
	Data->SetNumberField(TEXT("total_sections"), Montage->CompositeSections.Num());
	return MakeResponse(true, Data);
}

FString FMCPServer::HandleReadMontage(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString MontagePath;
	if (!Params->TryGetStringField(TEXT("path"), MontagePath))
		return MakeError(TEXT("path required"));

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
	if (!Montage)
		return MakeError(FString::Printf(TEXT("Montage not found: %s"), *MontagePath));

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), MontagePath);
	Data->SetNumberField(TEXT("length"), Montage->GetPlayLength());
	Data->SetStringField(TEXT("skeleton"), Montage->GetSkeleton() ? Montage->GetSkeleton()->GetPathName() : TEXT("None"));

	// Sections
	TArray<TSharedPtr<FJsonValue>> Sections;
	for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
	{
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("name"), Montage->CompositeSections[i].SectionName.ToString());
		S->SetNumberField(TEXT("time"), Montage->CompositeSections[i].GetTime());
		Sections.Add(MakeShared<FJsonValueObject>(S));
	}
	Data->SetArrayField(TEXT("sections"), Sections);

	// Slot tracks
	TArray<TSharedPtr<FJsonValue>> Slots;
	for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetStringField(TEXT("slot_name"), SlotTrack.SlotName.ToString());
		Slots.Add(MakeShared<FJsonValueObject>(SlotObj));
	}
	Data->SetArrayField(TEXT("slots"), Slots);

	// Notifies
	TArray<TSharedPtr<FJsonValue>> Notifies;
	for (const FAnimNotifyEvent& Notify : Montage->Notifies)
	{
		TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
		N->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
		N->SetNumberField(TEXT("time"), Notify.GetTriggerTime());
		N->SetStringField(TEXT("class"), Notify.Notify ? Notify.Notify->GetClass()->GetName() : TEXT("None"));
		Notifies.Add(MakeShared<FJsonValueObject>(N));
	}
	Data->SetArrayField(TEXT("notifies"), Notifies);

	return MakeResponse(true, Data);
}

// ===== BLEND SPACE =====

FString FMCPServer::HandleCreateBlendSpace(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString AssetPath, AssetName, SkeletonPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return MakeError(TEXT("asset_path required"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return MakeError(TEXT("asset_name required"));
	if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
		return MakeError(TEXT("skeleton_path required"));

	bool bIs1D = false;
	Params->TryGetBoolField(TEXT("is_1d"), bIs1D);

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
		return MakeError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UFactory* Factory = nullptr;
	UClass* AssetClass = nullptr;
	if (bIs1D)
	{
		UBlendSpaceFactory1D* BSFactory = NewObject<UBlendSpaceFactory1D>();
		BSFactory->TargetSkeleton = Skeleton;
		Factory = BSFactory;
		AssetClass = UBlendSpace1D::StaticClass();
	}
	else
	{
		UBlendSpaceFactoryNew* BSFactory = NewObject<UBlendSpaceFactoryNew>();
		BSFactory->TargetSkeleton = Skeleton;
		Factory = BSFactory;
		AssetClass = UBlendSpace::StaticClass();
	}

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, AssetPath, AssetClass, Factory);
	if (!NewAsset)
		return MakeError(TEXT("Failed to create BlendSpace"));

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName(), false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Data->SetBoolField(TEXT("is_1d"), bIs1D);
	Data->SetStringField(TEXT("skeleton"), SkeletonPath);
	return MakeResponse(true, Data);
}

FString FMCPServer::HandleAddBlendSpaceSample(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString BlendSpacePath, AnimPath;
	if (!Params->TryGetStringField(TEXT("blend_space_path"), BlendSpacePath))
		return MakeError(TEXT("blend_space_path required"));
	if (!Params->TryGetStringField(TEXT("animation_path"), AnimPath))
		return MakeError(TEXT("animation_path required (AnimSequence asset path)"));

	double ValueX = 0, ValueY = 0;
	Params->TryGetNumberField(TEXT("value_x"), ValueX);
	Params->TryGetNumberField(TEXT("value_y"), ValueY);

	UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *BlendSpacePath);
	if (!BlendSpace)
		return MakeError(FString::Printf(TEXT("BlendSpace not found: %s"), *BlendSpacePath));

	UAnimSequence* AnimSeq = LoadObject<UAnimSequence>(nullptr, *AnimPath);
	if (!AnimSeq)
		return MakeError(FString::Printf(TEXT("AnimSequence not found: %s"), *AnimPath));

	// Add sample via editing API
	BlendSpace->Modify();
	BlendSpace->AddSample(AnimSeq, FVector(ValueX, ValueY, 0.0));

	UEditorAssetLibrary::SaveAsset(BlendSpacePath, false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("animation"), AnimPath);
	Data->SetNumberField(TEXT("value_x"), ValueX);
	Data->SetNumberField(TEXT("value_y"), ValueY);
	Data->SetNumberField(TEXT("total_samples"), BlendSpace->GetBlendSamples().Num());
	return MakeResponse(true, Data);
}

// ===== ANIM SEQUENCE READING =====

FString FMCPServer::HandleReadAnimSequence(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString AnimPath;
	if (!Params->TryGetStringField(TEXT("path"), AnimPath))
		return MakeError(TEXT("path required"));

	UAnimSequenceBase* AnimSeq = LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);
	if (!AnimSeq)
		return MakeError(FString::Printf(TEXT("Animation not found: %s"), *AnimPath));

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), AnimPath);
	Data->SetStringField(TEXT("class"), AnimSeq->GetClass()->GetName());
	Data->SetNumberField(TEXT("length"), AnimSeq->GetPlayLength());
	Data->SetStringField(TEXT("skeleton"), AnimSeq->GetSkeleton() ? AnimSeq->GetSkeleton()->GetPathName() : TEXT("None"));

	// Notifies
	TArray<TSharedPtr<FJsonValue>> Notifies;
	for (const FAnimNotifyEvent& Notify : AnimSeq->Notifies)
	{
		TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
		N->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
		N->SetNumberField(TEXT("time"), Notify.GetTriggerTime());
		if (Notify.Notify)
			N->SetStringField(TEXT("notify_class"), Notify.Notify->GetClass()->GetName());
		if (Notify.NotifyStateClass.Get())
			N->SetStringField(TEXT("state_class"), Notify.NotifyStateClass.Get()->GetClass()->GetName());
		Notifies.Add(MakeShared<FJsonValueObject>(N));
	}
	Data->SetArrayField(TEXT("notifies"), Notifies);
	Data->SetNumberField(TEXT("notify_count"), Notifies.Num());

	// Rate scale
	if (UAnimSequence* Seq = Cast<UAnimSequence>(AnimSeq))
	{
		Data->SetNumberField(TEXT("rate_scale"), Seq->RateScale);
		Data->SetNumberField(TEXT("num_frames"), Seq->GetNumberOfSampledKeys());
	}

	return MakeResponse(true, Data);
}

// ===== ANIM BLUEPRINT CREATION =====

FString FMCPServer::HandleCreateAnimBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString AssetPath, AssetName, SkeletonPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return MakeError(TEXT("asset_path required (e.g. /Game/Blueprints/Animations)"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return MakeError(TEXT("asset_name required"));
	if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
		return MakeError(TEXT("skeleton_path required (path to USkeleton asset)"));

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
		return MakeError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->TargetSkeleton = Skeleton;
	Factory->ParentClass = UAnimInstance::StaticClass();

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, AssetPath, UAnimBlueprint::StaticClass(), Factory);
	if (!NewAsset)
		return MakeError(TEXT("Failed to create AnimBlueprint"));

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName(), false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Data->SetStringField(TEXT("skeleton"), SkeletonPath);
	return MakeResponse(true, Data);
}

// Creates a Post Process Anim Blueprint with ModifyBone nodes for spine and head.
// Assign this ABP to the mesh and update its FAnimNode_ModifyBone rotations from
// runtime code. Direct node updates avoid exposed-pin evaluation overwriting them.
FString FMCPServer::HandleSetupFpSpinePitchAbp(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString AssetPath, AssetName, SkeletonPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return MakeError(TEXT("asset_path required"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return MakeError(TEXT("asset_name required"));
	if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
		return MakeError(TEXT("skeleton_path required"));

	FString BoneName = TEXT("spine_01");
	Params->TryGetStringField(TEXT("bone_name"), BoneName);

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
		return MakeError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	// ------ 0. Delete existing asset if present (so we can recreate cleanly) ------
	FString FullAssetPath = AssetPath / AssetName;
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		UEditorAssetLibrary::DeleteAsset(FullAssetPath);
	}

	// ------ 1. Create the AnimBlueprint ------
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->TargetSkeleton = Skeleton;
	Factory->ParentClass = UAnimInstance::StaticClass();

	UAnimBlueprint* ABP = Cast<UAnimBlueprint>(
		AssetTools.CreateAsset(AssetName, AssetPath, UAnimBlueprint::StaticClass(), Factory));
	if (!ABP)
		return MakeError(TEXT("Failed to create AnimBlueprint"));

	// ------ 2. Find the AnimGraph ------
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == TEXT("AnimGraph"))
		{
			AnimGraph = Graph;
			break;
		}
	}
	if (!AnimGraph)
		return MakeError(TEXT("AnimGraph not found in newly created ABP"));

	// ------ 4. Find existing Root node ------
	UAnimGraphNode_Root* RootNode = nullptr;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (UAnimGraphNode_Root* Root = Cast<UAnimGraphNode_Root>(Node))
		{
			RootNode = Root;
			break;
		}
	}
	if (!RootNode)
		return MakeError(TEXT("AnimGraphNode_Root not found"));

	// ------ 4.5. Create LinkedInputPose node (provides parent ABP's animated pose) ------
	// This is the correct way to get the parent ABP's output in a post-process ABP.
	// Without this, ModifyBone.ComponentPose is unconnected → reference pose → T-pose.
	UAnimGraphNode_LinkedInputPose* LinkedInputPoseNode = NewObject<UAnimGraphNode_LinkedInputPose>(AnimGraph);
	LinkedInputPoseNode->CreateNewGuid();
	AnimGraph->AddNode(LinkedInputPoseNode, false);
	LinkedInputPoseNode->NodePosX = RootNode->NodePosX - 700;
	LinkedInputPoseNode->NodePosY = RootNode->NodePosY;
	LinkedInputPoseNode->AllocateDefaultPins();

	// ------ 5. Create ModifyBone node for spine (BoneName param, typically spine_01) ------
	auto MakeModifyBoneNode = [&](const FString& Bone, int32 PosXOffset) -> UAnimGraphNode_ModifyBone*
	{
		UAnimGraphNode_ModifyBone* N = NewObject<UAnimGraphNode_ModifyBone>(AnimGraph);
		N->CreateNewGuid();
		N->Node.BoneToModify.BoneName = FName(*Bone);
		N->Node.RotationMode = EBoneModificationMode::BMM_Additive;
		N->Node.RotationSpace = EBoneControlSpace::BCS_WorldSpace; // World space: Pitch = forward tilt
		N->Node.TranslationMode = EBoneModificationMode::BMM_Ignore;
		N->Node.ScaleMode = EBoneModificationMode::BMM_Ignore;
		N->Node.Alpha = 1.0f;
		AnimGraph->AddNode(N, false);
		N->NodePosX = RootNode->NodePosX + PosXOffset;
		N->NodePosY = RootNode->NodePosY;
		N->AllocateDefaultPins();
		return N;
	};

	UAnimGraphNode_ModifyBone* SpineNode = MakeModifyBoneNode(BoneName, -500);  // spine_01
	UAnimGraphNode_ModifyBone* HeadNode  = MakeModifyBoneNode(TEXT("head"),  -300);  // head

	// ------ 6. LocalToComponentSpace: LinkedInputPose[local] → L2CS → SpineNode[component] ------
	UAnimGraphNode_LocalToComponentSpace* LocalToCSNode = NewObject<UAnimGraphNode_LocalToComponentSpace>(AnimGraph);
	LocalToCSNode->CreateNewGuid();
	AnimGraph->AddNode(LocalToCSNode, false);
	LocalToCSNode->NodePosX = RootNode->NodePosX - 700;
	LocalToCSNode->NodePosY = RootNode->NodePosY;
	LocalToCSNode->AllocateDefaultPins();

	UEdGraphPin* LinkedPoseOutPin   = LinkedInputPoseNode->FindPin(TEXT("Pose"), EGPD_Output);
	UEdGraphPin* LocalToCSInputPin  = LocalToCSNode->FindPin(TEXT("LocalPose"));
	UEdGraphPin* LocalToCSOutputPin = LocalToCSNode->FindPin(TEXT("ComponentPose"));
	UEdGraphPin* SpineCompPosePin   = SpineNode->FindPin(TEXT("ComponentPose"));

	bool bLinkedPoseConnected = false;
	if (LinkedPoseOutPin && LocalToCSInputPin)  { LinkedPoseOutPin->MakeLinkTo(LocalToCSInputPin);  bLinkedPoseConnected = true; }
	if (LocalToCSOutputPin && SpineCompPosePin) { LocalToCSOutputPin->MakeLinkTo(SpineCompPosePin); }

	// Chain: SpineNode.Pose → HeadNode.ComponentPose
	UEdGraphPin* SpineOutPin      = SpineNode->FindPin(TEXT("Pose"));
	UEdGraphPin* HeadCompPosePin  = HeadNode->FindPin(TEXT("ComponentPose"));
	if (SpineOutPin && HeadCompPosePin) { SpineOutPin->MakeLinkTo(HeadCompPosePin); }

	// ------ 7. ComponentToLocalSpace: HeadNode[component] → CS2L → Root[local] ------
	UAnimGraphNode_ComponentToLocalSpace* CSToLocalNode = NewObject<UAnimGraphNode_ComponentToLocalSpace>(AnimGraph);
	CSToLocalNode->CreateNewGuid();
	AnimGraph->AddNode(CSToLocalNode, false);
	CSToLocalNode->NodePosX = RootNode->NodePosX - 100;
	CSToLocalNode->NodePosY = RootNode->NodePosY;
	CSToLocalNode->AllocateDefaultPins();

	UEdGraphPin* HeadOutPin         = HeadNode->FindPin(TEXT("Pose"));
	UEdGraphPin* CSToLocalInputPin  = CSToLocalNode->FindPin(TEXT("ComponentPose"));
	UEdGraphPin* CSToLocalOutputPin = CSToLocalNode->FindPin(TEXT("Pose"), EGPD_Output);
	UEdGraphPin* RootInputPin       = RootNode->FindPin(TEXT("Result"));

	if (HeadOutPin && CSToLocalInputPin)    { HeadOutPin->MakeLinkTo(CSToLocalInputPin);    }
	if (CSToLocalOutputPin && RootInputPin) { CSToLocalOutputPin->MakeLinkTo(RootInputPin); }

	// ------ 9. Compile and save ------
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ABP);
	FKismetEditorUtilities::CompileBlueprint(ABP, EBlueprintCompileOptions::SkipGarbageCollection);
	UEditorAssetLibrary::SaveAsset(ABP->GetPathName(), false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), ABP->GetPathName());
	Data->SetStringField(TEXT("spine_bone"), BoneName);
	Data->SetBoolField(TEXT("linked_pose_connected"), bLinkedPoseConnected);
	Data->SetBoolField(TEXT("spine_chain_ok"), LocalToCSOutputPin != nullptr && SpineCompPosePin != nullptr && SpineOutPin != nullptr && HeadCompPosePin != nullptr);
	Data->SetBoolField(TEXT("head_to_root_ok"), HeadOutPin != nullptr && CSToLocalInputPin != nullptr && CSToLocalOutputPin != nullptr && RootInputPin != nullptr);
	return MakeResponse(true, Data);
}

// Retargets an animation asset (AnimSequence, AnimMontage, etc.) from its
// current Skeleton onto a different one by bone name, using the same
// underlying EditorAnimUtils::RetargetAnimations() the classic "Retarget
// Skeleton" right-click tool in the Content Browser calls. This is plain
// C++ (namespace free functions in the UnrealEd module), not exposed to
// Blueprint or Python at all - there is no scriptable way to do this
// without a tool like this one. Only works well when the two skeletons
// share compatible bone names/hierarchy (no IK Rig retargeting math is
// applied here, just remapping which Skeleton asset the animation data is
// interpreted against) - for skeletons with different proportions or bone
// naming, a proper IK Rig/IK Retargeter setup is needed instead, which
// this does not attempt.
FString FMCPServer::HandleRetargetAnimAsset(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString SourceAssetPath, TargetSkeletonPath, DestFolder;
	if (!Params->TryGetStringField(TEXT("source_asset_path"), SourceAssetPath))
		return MakeError(TEXT("source_asset_path required"));
	if (!Params->TryGetStringField(TEXT("target_skeleton_path"), TargetSkeletonPath))
		return MakeError(TEXT("target_skeleton_path required"));
	if (!Params->TryGetStringField(TEXT("dest_folder"), DestFolder))
		return MakeError(TEXT("dest_folder required (e.g. '/Game/ALSHost/Animations')"));

	FString Suffix = Params->HasField(TEXT("name_suffix")) ? Params->GetStringField(TEXT("name_suffix")) : TEXT("");
	FString Prefix = Params->HasField(TEXT("name_prefix")) ? Params->GetStringField(TEXT("name_prefix")) : TEXT("");
	bool bConvertSpace = true;
	Params->TryGetBoolField(TEXT("convert_space"), bConvertSpace);

	UAnimationAsset* SourceAsset = LoadObject<UAnimationAsset>(nullptr, *SourceAssetPath);
	if (!SourceAsset)
		return MakeError(FString::Printf(TEXT("Animation asset not found: %s"), *SourceAssetPath));

	USkeleton* NewSkeleton = LoadObject<USkeleton>(nullptr, *TargetSkeletonPath);
	if (!NewSkeleton)
		return MakeError(FString::Printf(TEXT("Target skeleton not found: %s"), *TargetSkeletonPath));

	USkeleton* OldSkeleton = SourceAsset->GetSkeleton();
	if (!OldSkeleton)
		return MakeError(TEXT("Source asset has no Skeleton set"));

	if (OldSkeleton == NewSkeleton)
		return MakeError(TEXT("Source asset already targets the requested skeleton"));

	EditorAnimUtils::FNameDuplicationRule NameRule;
	NameRule.FolderPath = DestFolder;
	NameRule.Prefix = Prefix;
	NameRule.Suffix = Suffix;

	TArray<TWeakObjectPtr<UObject>> AssetsToRetarget;
	AssetsToRetarget.Add(SourceAsset);

	UObject* RetargetedAsset = EditorAnimUtils::RetargetAnimations(
		OldSkeleton, NewSkeleton, AssetsToRetarget,
		/*bRetargetReferredAssets=*/true, &NameRule, bConvertSpace);

	if (!RetargetedAsset)
		return MakeError(TEXT("RetargetAnimations returned no asset - check the source/target skeletons share compatible bone names"));

	UEditorAssetLibrary::SaveAsset(RetargetedAsset->GetPathName(), false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("source_path"), SourceAssetPath);
	Data->SetStringField(TEXT("retargeted_path"), RetargetedAsset->GetPathName());
	Data->SetStringField(TEXT("target_skeleton"), TargetSkeletonPath);
	return MakeResponse(true, Data);
}

static TSharedPtr<FJsonObject> SerializeTransform(const FTransform& Transform)
{
	const FVector Loc = Transform.GetLocation();
	const FRotator Rot = Transform.Rotator();
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Loc.X);
	LocObj->SetNumberField(TEXT("y"), Loc.Y);
	LocObj->SetNumberField(TEXT("z"), Loc.Z);
	TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
	RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
	Obj->SetObjectField(TEXT("location"), LocObj);
	Obj->SetObjectField(TEXT("rotation"), RotObj);
	return Obj;
}

// Compares a bone's component-space transform between two animations at two
// given times - built to answer "what offset would make this weapon's grip
// bone match a known-good animation's grip bone", since our HeldObjectRoot
// offset is a single constant applied relative to a fixed virtual bone that
// itself rigidly follows this exact real bone (hand_r) every frame. If the
// mismatch is a genuinely constant offset (not a per-frame drift), "delta"
// below is directly usable as ReloadHeldObjectLocationOffset/RotationOffset.
FString FMCPServer::HandleCompareAnimBonePose(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return MakeError(TEXT("Missing params"));

	FString AnimPathA, AnimPathB, BoneName;
	double TimeA = 0.0, TimeB = 0.0;
	if (!Params->TryGetStringField(TEXT("anim_path_a"), AnimPathA))
		return MakeError(TEXT("anim_path_a required"));
	if (!Params->TryGetStringField(TEXT("anim_path_b"), AnimPathB))
		return MakeError(TEXT("anim_path_b required"));
	if (!Params->TryGetNumberField(TEXT("time_a"), TimeA))
		return MakeError(TEXT("time_a required (seconds)"));
	if (!Params->TryGetNumberField(TEXT("time_b"), TimeB))
		return MakeError(TEXT("time_b required (seconds)"));
	if (!Params->TryGetStringField(TEXT("bone_name"), BoneName))
		BoneName = TEXT("hand_r");

	UAnimSequenceBase* AnimA = LoadObject<UAnimSequenceBase>(nullptr, *AnimPathA);
	if (!AnimA)
		return MakeError(FString::Printf(TEXT("Animation not found: %s"), *AnimPathA));

	UAnimSequenceBase* AnimB = LoadObject<UAnimSequenceBase>(nullptr, *AnimPathB);
	if (!AnimB)
		return MakeError(FString::Printf(TEXT("Animation not found: %s"), *AnimPathB));

	FAnimPoseEvaluationOptions EvalOptions;
	FAnimPose PoseA, PoseB;
	UAnimPoseExtensions::GetAnimPoseAtTime(AnimA, TimeA, EvalOptions, PoseA);
	UAnimPoseExtensions::GetAnimPoseAtTime(AnimB, TimeB, EvalOptions, PoseB);

	TArray<FName> BoneNamesA, BoneNamesB;
	UAnimPoseExtensions::GetBoneNames(PoseA, BoneNamesA);
	UAnimPoseExtensions::GetBoneNames(PoseB, BoneNamesB);
	if (!BoneNamesA.Contains(FName(*BoneName)))
		return MakeError(FString::Printf(TEXT("Bone '%s' not found in %s"), *BoneName, *AnimPathA));
	if (!BoneNamesB.Contains(FName(*BoneName)))
		return MakeError(FString::Printf(TEXT("Bone '%s' not found in %s"), *BoneName, *AnimPathB));

	const FTransform TransformA = UAnimPoseExtensions::GetBonePose(PoseA, FName(*BoneName), EAnimPoseSpaces::World);
	const FTransform TransformB = UAnimPoseExtensions::GetBonePose(PoseB, FName(*BoneName), EAnimPoseSpaces::World);

	// B relative to A: the transform that, applied on top of A, produces B.
	const FTransform DeltaBRelativeToA = TransformB.GetRelativeTransform(TransformA);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bone_name"), BoneName);
	Data->SetObjectField(TEXT("pose_a"), SerializeTransform(TransformA));
	Data->SetObjectField(TEXT("pose_b"), SerializeTransform(TransformB));
	Data->SetObjectField(TEXT("delta_b_relative_to_a"), SerializeTransform(DeltaBRelativeToA));
	return MakeResponse(true, Data);
}
