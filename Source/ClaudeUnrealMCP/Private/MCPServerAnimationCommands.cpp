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
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"

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
