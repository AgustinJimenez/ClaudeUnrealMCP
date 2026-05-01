#include "MCPServer.h"
#include "Dom/JsonObject.h"
#include "EditorAssetLibrary.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

// Serialize a UObject's properties into a JSON object for inspection.
// Recursively inlines Instanced sub-objects up to MaxDepth.
static TSharedPtr<FJsonObject> SerializeObjectToJson(UObject* Object, int32 Depth, int32 MaxDepth)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	if (!Object || Depth > MaxDepth)
	{
		return Out;
	}

	Out->SetStringField(TEXT("__class"), Object->GetClass()->GetName());
	Out->SetStringField(TEXT("__path"), Object->GetPathName());

	for (TFieldIterator<FProperty> PropIt(Object->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		const FString PropName = Prop->GetName();

		// Inlined sub-object (Instanced)
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			UObject* SubObj = ObjProp->GetObjectPropertyValue_InContainer(Object);
			if (SubObj)
			{
				if (Prop->HasAnyPropertyFlags(CPF_InstancedReference) && Depth < MaxDepth)
				{
					Out->SetObjectField(PropName, SerializeObjectToJson(SubObj, Depth + 1, MaxDepth));
				}
				else
				{
					Out->SetStringField(PropName, SubObj->GetPathName());
				}
			}
			else
			{
				Out->SetField(PropName, MakeShared<FJsonValueNull>());
			}
			continue;
		}

		// Arrays
		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Object));
			const int32 Num = Helper.Num();
			for (int32 Idx = 0; Idx < Num; ++Idx)
			{
				uint8* ElemPtr = Helper.GetRawPtr(Idx);
				if (FObjectProperty* ElemObj = CastField<FObjectProperty>(ArrProp->Inner))
				{
					UObject* SubObj = ElemObj->GetObjectPropertyValue(ElemPtr);
					if (SubObj && ArrProp->Inner->HasAnyPropertyFlags(CPF_InstancedReference) && Depth < MaxDepth)
					{
						Items.Add(MakeShared<FJsonValueObject>(SerializeObjectToJson(SubObj, Depth + 1, MaxDepth)));
					}
					else
					{
						Items.Add(MakeShared<FJsonValueString>(SubObj ? SubObj->GetPathName() : TEXT("None")));
					}
				}
				else
				{
					FString ElemStr;
					ArrProp->Inner->ExportText_Direct(ElemStr, ElemPtr, ElemPtr, nullptr, PPF_None);
					Items.Add(MakeShared<FJsonValueString>(ElemStr));
				}
			}
			Out->SetArrayField(PropName, Items);
			continue;
		}

		// Simple properties (numbers, strings, enums, structs) — export as text
		FString ValStr;
		Prop->ExportText_InContainer(0, ValStr, Object, Object, Object, PPF_None);
		if (!ValStr.IsEmpty())
		{
			Out->SetStringField(PropName, ValStr);
		}
	}
	return Out;
}

FString FMCPServer::HandleRunPython(const TSharedPtr<FJsonObject>& Params)
{
	// Python plugin caused editor crashes due to PostLoad assertion when enabled in uproject.
	// Provide targeted asset manipulation commands via this handler instead (repurposed).
	// Operation parameter chooses what to do:
	//   op=duplicate_asset: params.source_path, params.dest_path
	//   op=does_asset_exist: params.path
	//   op=save_asset: params.path

	if (!Params.IsValid())
	{
		return MakeError(TEXT("Missing params"));
	}

	FString Op;
	if (!Params->TryGetStringField(TEXT("op"), Op) || Op.IsEmpty())
	{
		return MakeError(TEXT("Missing 'op' parameter. Supported: duplicate_asset, does_asset_exist, save_asset"));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

	if (Op == TEXT("duplicate_asset"))
	{
		FString SourcePath, DestPath;
		if (!Params->TryGetStringField(TEXT("source_path"), SourcePath) ||
			!Params->TryGetStringField(TEXT("dest_path"), DestPath))
		{
			return MakeError(TEXT("duplicate_asset requires source_path and dest_path"));
		}

		if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
		{
			return MakeError(FString::Printf(TEXT("Source asset not found: %s"), *SourcePath));
		}

		if (UEditorAssetLibrary::DoesAssetExist(DestPath))
		{
			Data->SetBoolField(TEXT("already_exists"), true);
			Data->SetStringField(TEXT("dest_path"), DestPath);
			return MakeResponse(true, Data);
		}

		UObject* NewAsset = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);
		if (!NewAsset)
		{
			return MakeError(FString::Printf(TEXT("Failed to duplicate %s to %s"), *SourcePath, *DestPath));
		}

		UEditorAssetLibrary::SaveAsset(DestPath, false);
		Data->SetStringField(TEXT("dest_path"), DestPath);
		Data->SetBoolField(TEXT("already_exists"), false);
		return MakeResponse(true, Data);
	}
	else if (Op == TEXT("does_asset_exist"))
	{
		FString Path;
		if (!Params->TryGetStringField(TEXT("path"), Path))
		{
			return MakeError(TEXT("does_asset_exist requires path"));
		}
		Data->SetBoolField(TEXT("exists"), UEditorAssetLibrary::DoesAssetExist(Path));
		return MakeResponse(true, Data);
	}
	else if (Op == TEXT("save_asset"))
	{
		FString Path;
		if (!Params->TryGetStringField(TEXT("path"), Path))
		{
			return MakeError(TEXT("save_asset requires path"));
		}
		const bool bOK = UEditorAssetLibrary::SaveAsset(Path, false);
		Data->SetBoolField(TEXT("saved"), bOK);
		return MakeResponse(bOK, Data);
	}
	else if (Op == TEXT("inspect_asset"))
	{
		FString Path;
		if (!Params->TryGetStringField(TEXT("path"), Path))
		{
			return MakeError(TEXT("inspect_asset requires path"));
		}
		int32 MaxDepth = 4;
		Params->TryGetNumberField(TEXT("max_depth"), MaxDepth);

		UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
		if (!Asset)
		{
			return MakeError(FString::Printf(TEXT("Could not load asset: %s"), *Path));
		}
		Data->SetObjectField(TEXT("asset"), SerializeObjectToJson(Asset, 0, MaxDepth));
		return MakeResponse(true, Data);
	}
	else if (Op == TEXT("set_property"))
	{
		// Set a top-level property on an asset object by name.
		// params: path, property, value (string, parsed as FProperty text import)
		FString Path, PropName, ValueText;
		if (!Params->TryGetStringField(TEXT("path"), Path) ||
			!Params->TryGetStringField(TEXT("property"), PropName) ||
			!Params->TryGetStringField(TEXT("value"), ValueText))
		{
			return MakeError(TEXT("set_property requires path, property, value"));
		}
		UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
		if (!Asset)
		{
			return MakeError(FString::Printf(TEXT("Could not load asset: %s"), *Path));
		}
		FProperty* Prop = FindFProperty<FProperty>(Asset->GetClass(), *PropName);
		if (!Prop)
		{
			return MakeError(FString::Printf(TEXT("Property not found: %s"), *PropName));
		}
		void* ValPtr = Prop->ContainerPtrToValuePtr<void>(Asset);
		const TCHAR* Result = Prop->ImportText_Direct(*ValueText, ValPtr, Asset, PPF_None);
		if (!Result)
		{
			return MakeError(FString::Printf(TEXT("Failed to import value '%s' for property %s"), *ValueText, *PropName));
		}
		Asset->Modify();
		UEditorAssetLibrary::SaveAsset(Path, false);
		Data->SetBoolField(TEXT("set"), true);
		return MakeResponse(true, Data);
	}

	return MakeError(FString::Printf(TEXT("Unknown op: %s"), *Op));
}
