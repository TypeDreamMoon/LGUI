// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIPrefabBehaviourUtils.h"
#include "PrefabSystem/LGUIPrefab.h"
#include "Core/LGUILifeCycleBehaviour.h"
#include "Core/ActorComponent/UIItem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabBehaviourUtils"

namespace LGUIPrefabBehaviourUtils
{

// ComponentTags entry marking the companion component. Serialized with the prefab, so the
// companion stays identified across sessions. NEVER match "any blueprint behaviour" instead:
// reusable behaviour blueprints (fade/hover/...) legitimately live on prefab roots, and
// promoting variables into a shared asset would pollute it for every other user.
const FName CompanionComponentTag(TEXT("LGUIPrefabCompanionBehaviour"));

UActorComponent* FindBehaviourComponent(AActor* InPrefabRootActor)
{
	if (InPrefabRootActor == nullptr)return nullptr;
	for (UActorComponent* Comp : InPrefabRootActor->GetComponents())
	{
		if (Comp != nullptr
			&& Comp->ComponentHasTag(CompanionComponentTag)
			&& Comp->IsA<ULGUILifeCycleBehaviour>()
			&& Comp->GetClass()->ClassGeneratedBy != nullptr)
		{
			return Comp;
		}
	}
	return nullptr;
}

UBlueprint* FindBehaviourBlueprint(AActor* InPrefabRootActor)
{
	if (auto Comp = FindBehaviourComponent(InPrefabRootActor))
	{
		return Cast<UBlueprint>(Comp->GetClass()->ClassGeneratedBy);
	}
	return nullptr;
}

static void AttachCompanionComponent(UBlueprint* InBlueprint, AActor* InPrefabRootActor)
{
	InPrefabRootActor->Modify();
	UClass* GeneratedClass = InBlueprint->GeneratedClass;
	auto Component = NewObject<UActorComponent>(InPrefabRootActor, GeneratedClass
		, *FComponentEditorUtils::GenerateValidVariableName(GeneratedClass, InPrefabRootActor), RF_Transactional);
	Component->ComponentTags.Add(CompanionComponentTag);
	InPrefabRootActor->AddInstanceComponent(Component);
	Component->RegisterComponent();
}

UBlueprint* CreateBehaviourBlueprint(ULGUIPrefab* InPrefab, AActor* InPrefabRootActor)
{
	if (InPrefab == nullptr || InPrefabRootActor == nullptr)return nullptr;

	// asset named after the prefab, in the prefab's folder
	const FString PackagePath = FPackageName::GetLongPackagePath(InPrefab->GetOutermost()->GetName());
	const FString BaseName = FString::Printf(TEXT("BP_%s"), *InPrefab->GetName());

	// an orphaned companion asset may already exist (component lost without saving the
	// prefab): re-attach it instead of minting BP_<PrefabName>1 and splitting the logic
	if (UBlueprint* ExistingBlueprint = LoadObject<UBlueprint>(nullptr, *(PackagePath / BaseName + TEXT(".") + BaseName)))
	{
		if (ExistingBlueprint->GeneratedClass != nullptr
			&& ExistingBlueprint->GeneratedClass->IsChildOf(ULGUILifeCycleBehaviour::StaticClass()))
		{
			AttachCompanionComponent(ExistingBlueprint, InPrefabRootActor);
			return ExistingBlueprint;
		}
	}

	FString PackageName, AssetName;
	FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(PackagePath / BaseName, TEXT(""), PackageName, AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)return nullptr;
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ULGUILifeCycleBehaviour::StaticClass(), Package, *AssetName
		, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (Blueprint == nullptr)return nullptr;
	FAssetRegistryModule::AssetCreated(Blueprint);
	Package->MarkPackageDirty();

	AttachCompanionComponent(Blueprint, InPrefabRootActor);
	return Blueprint;
}

FString MakeVariableNameForTarget(UObject* InTarget)
{
	FString Raw;
	if (auto Actor = Cast<AActor>(InTarget))
	{
		Raw = Actor->GetActorLabel();
	}
	else if (auto Component = Cast<UActorComponent>(InTarget))
	{
		// component of the selected actor: the actor label carries the user's intent
		// ("StartButton"); only fall back to the component name for anonymous actors
		Raw = Component->GetOwner() != nullptr ? Component->GetOwner()->GetActorLabel() : Component->GetName();
	}
	else if (InTarget != nullptr)
	{
		Raw = InTarget->GetName();
	}

	// sanitize to an identifier: alnum/underscore, non-ASCII kept (FName and blueprint
	// variable names handle CJK labels fine; flattening them to '_' would collide them all)
	FString Result;
	Result.Reserve(Raw.Len());
	for (TCHAR Char : Raw)
	{
		Result.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') || Char > 0x7F ? Char : TEXT('_'));
	}
	if (Result.IsEmpty())
	{
		Result = TEXT("Element");
	}
	if (FChar::IsDigit(Result[0]))
	{
		Result.InsertAt(0, TEXT('_'));
	}
	return Result;
}

namespace
{
	enum class EDeclareVariableResult { Added, Reused, Incompatible, Failed };

	/**
	 * Declare (or accept an existing compatible) member variable named InVarName able to hold
	 * InTargetClass. New variables and reused ones are made Instance Editable: LGUI's prefab
	 * writer skips CPF_DisableEditOnInstance properties, so a variable keeping the blueprint
	 * default flag would LOOK bound in the editor and come back null after reload.
	 */
	EDeclareVariableResult DeclareVariable(UBlueprint* InBlueprint, const FName InVarName, UClass* InTargetClass)
	{
		const int32 ExistingVarIndex = FBlueprintEditorUtils::FindNewVariableIndex(InBlueprint, InVarName);
		if (ExistingVarIndex == INDEX_NONE)
		{
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = InTargetClass;
			if (!FBlueprintEditorUtils::AddMemberVariable(InBlueprint, InVarName, PinType))
			{
				// name taken by an inherited/native member -- caller may retry with a suffix
				return EDeclareVariableResult::Failed;
			}
			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(InBlueprint, InVarName, false);
			return EDeclareVariableResult::Added;
		}

		const FBPVariableDescription& ExistingVar = InBlueprint->NewVariables[ExistingVarIndex];
		UClass* ExistingClass = Cast<UClass>(ExistingVar.VarType.PinSubCategoryObject.Get());
		if (ExistingVar.VarType.PinCategory != UEdGraphSchema_K2::PC_Object
			|| ExistingClass == nullptr || !InTargetClass->IsChildOf(ExistingClass))
		{
			return EDeclareVariableResult::Incompatible;
		}
		if ((ExistingVar.PropertyFlags & CPF_DisableEditOnInstance) != 0)
		{
			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(InBlueprint, InVarName, false);
		}
		return EDeclareVariableResult::Reused;
	}
}

bool PromoteToVariable(UBlueprint* InBlueprint, AActor* InPrefabRootActor, UObject* InTarget, const FString& InVariableName, FText& OutMessage)
{
	if (InBlueprint == nullptr || InPrefabRootActor == nullptr || InTarget == nullptr)
	{
		OutMessage = LOCTEXT("PromoteError_InvalidInput", "Invalid input.");
		return false;
	}
	if (InTarget->GetClass()->ClassGeneratedBy == InBlueprint)
	{
		OutMessage = LOCTEXT("PromoteError_SelfClass", "Cannot promote an instance of the behaviour blueprint itself.");
		return false;
	}
	UClass* TargetClass = InTarget->GetClass();

	// preferred name first; on collision with an incompatible/native member, disambiguate
	// with the target class ("Text" taken -> "Text_UIText") instead of dead-ending
	FString ClassSuffix = TargetClass->GetName();
	ClassSuffix.RemoveFromEnd(TEXT("Component"));
	const FString CandidateNames[] = { InVariableName, InVariableName + TEXT("_") + ClassSuffix };
	FName VarName = NAME_None;
	bool bAddedNewVariable = false;
	for (const FString& Candidate : CandidateNames)
	{
		const EDeclareVariableResult Result = DeclareVariable(InBlueprint, FName(*Candidate), TargetClass);
		if (Result == EDeclareVariableResult::Added || Result == EDeclareVariableResult::Reused)
		{
			VarName = FName(*Candidate);
			bAddedNewVariable = (Result == EDeclareVariableResult::Added);
			break;
		}
	}
	if (VarName.IsNone())
	{
		OutMessage = FText::Format(LOCTEXT("PromoteError_NoUsableName", "Neither \"{0}\" nor \"{1}\" can be used on {2} (taken by incompatible or inherited members). Rename the element and promote again.")
			, FText::FromString(CandidateNames[0]), FText::FromString(CandidateNames[1]), FText::FromString(InBlueprint->GetName()));
		return false;
	}

	// remember how to find the target again: compiling can reinstance components whose class
	// (or dependent classes) get recompiled, leaving InTarget pointing at a trash instance
	AActor* TargetOwnerActor = nullptr;
	FName TargetComponentName = NAME_None;
	if (auto TargetComp = Cast<UActorComponent>(InTarget))
	{
		TargetOwnerActor = TargetComp->GetOwner();
		TargetComponentName = TargetComp->GetFName();
	}

	// compile so GeneratedClass carries the property; skip when the compiled class is already
	// up to date (reusing a compiled variable must not be blocked by unrelated graph errors)
	const bool bNeedCompile = bAddedNewVariable
		|| FindFProperty<FObjectProperty>(InBlueprint->GeneratedClass, VarName) == nullptr;
	if (bNeedCompile)
	{
		FKismetEditorUtilities::CompileBlueprint(InBlueprint);
		if (InBlueprint->Status == BS_Error)
		{
			if (bAddedNewVariable)
			{
				// roll the half-added variable back out instead of leaving a broken declaration
				FBlueprintEditorUtils::RemoveMemberVariable(InBlueprint, VarName);
			}
			OutMessage = FText::Format(LOCTEXT("PromoteError_CompileFailed", "{0} failed to compile; fix its errors and promote again.")
				, FText::FromString(InBlueprint->GetName()));
			return false;
		}
	}

	// re-resolve both sides after a possible reinstancing pass
	UActorComponent* BehaviourComp = nullptr;
	for (UActorComponent* Comp : InPrefabRootActor->GetComponents())
	{
		if (Comp != nullptr && Comp->GetClass()->ClassGeneratedBy == InBlueprint)
		{
			BehaviourComp = Comp;
			break;
		}
	}
	if (BehaviourComp == nullptr)
	{
		OutMessage = FText::Format(LOCTEXT("PromoteError_NoInstance", "No {0} instance found on the prefab root actor.")
			, FText::FromString(InBlueprint->GetName()));
		return false;
	}
	if (!IsValid(InTarget) || InTarget->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
	{
		InTarget = nullptr;
		if (TargetOwnerActor != nullptr)
		{
			for (UActorComponent* Comp : TargetOwnerActor->GetComponents())
			{
				if (Comp != nullptr && Comp->GetFName() == TargetComponentName && IsValid(Comp))
				{
					InTarget = Comp;
					break;
				}
			}
		}
		if (InTarget == nullptr)
		{
			OutMessage = LOCTEXT("PromoteError_TargetGone", "The element was reinstanced during compile and could not be found again; promote again.");
			return false;
		}
	}

	auto Property = FindFProperty<FObjectProperty>(BehaviourComp->GetClass(), VarName);
	if (Property == nullptr || !InTarget->GetClass()->IsChildOf(Property->PropertyClass))
	{
		OutMessage = FText::Format(LOCTEXT("PromoteError_NoProperty", "Compiled class has no compatible property \"{0}\".")
			, FText::FromName(VarName));
		return false;
	}
	UObject* PreviousValue = Property->GetObjectPropertyValue_InContainer(BehaviourComp);
	BehaviourComp->Modify();
	Property->SetObjectPropertyValue_InContainer(BehaviourComp, InTarget);

	// replacing an existing binding is legal but must be said out loud, not disguised as a fresh promote
	if (PreviousValue != nullptr && PreviousValue != InTarget)
	{
		OutMessage = FText::Format(LOCTEXT("PromoteRebound", "Rebound {0}.{1}: {2} -> {3}")
			, FText::FromString(InBlueprint->GetName()), FText::FromName(VarName)
			, FText::FromString(PreviousValue->GetName()), FText::FromString(InTarget->GetName()));
	}
	else
	{
		OutMessage = FText::Format(LOCTEXT("PromoteSuccess", "{0}.{1} = {2}")
			, FText::FromString(InBlueprint->GetName()), FText::FromName(VarName), FText::FromString(InTarget->GetName()));
	}
	return true;
}

void AutoBindAndValidate(AActor* InPrefabRootActor, TArray<FString>& OutBoundDetails, TArray<FString>& OutProblems)
{
	OutBoundDetails.Reset();
	OutProblems.Reset();
	if (InPrefabRootActor == nullptr)return;

	// collect the prefab subtree once; keep ALL actors per sanitized label so ambiguity
	// is detected instead of silently binding to whichever actor traverses last
	TArray<AActor*> SubtreeActors;
	SubtreeActors.Add(InPrefabRootActor);
	for (int i = 0; i < SubtreeActors.Num(); i++)
	{
		TArray<AActor*> Children;
		SubtreeActors[i]->GetAttachedActors(Children);
		SubtreeActors.Append(Children);
	}
	TMap<FString, TArray<AActor*>> LabelToActors;
	for (AActor* Actor : SubtreeActors)
	{
		LabelToActors.FindOrAdd(MakeVariableNameForTarget(Actor)).Add(Actor);
	}

	for (UActorComponent* Comp : InPrefabRootActor->GetComponents())
	{
		if (Comp == nullptr || !Comp->IsA<ULGUILifeCycleBehaviour>() || Comp->GetClass()->ClassGeneratedBy == nullptr)
		{
			continue;
		}
		for (TFieldIterator<FObjectProperty> PropertyIt(Comp->GetClass()); PropertyIt; ++PropertyIt)
		{
			FObjectProperty* Property = *PropertyIt;
			// only blueprint-declared variables: native properties have their own semantics
			if (Property->GetOwnerClass() == nullptr || Cast<UBlueprintGeneratedClass>(Property->GetOwnerClass()) == nullptr)
			{
				continue;
			}
			const bool bSavableWithPrefab = (Property->PropertyFlags & CPF_DisableEditOnInstance) == 0;
			UObject* Value = Property->GetObjectPropertyValue_InContainer(Comp);
			if (Value != nullptr)
			{
				// dangling check on the VALUE itself (a deleted component is Garbage while its
				// owner stays valid), and prefab membership by subtree, not by world (the
				// preview world contains the root agent and world-default actors too)
				AActor* ValueActor = Cast<AActor>(Value);
				if (auto ValueComp = Cast<UActorComponent>(Value))
				{
					ValueActor = ValueComp->GetOwner();
				}
				if (!IsValid(Value) || ValueActor == nullptr || !SubtreeActors.Contains(ValueActor))
				{
					OutProblems.Add(FString::Printf(TEXT("%s.%s -> %s (deleted or not part of this prefab; the binding will not survive Apply)")
						, *Comp->GetName(), *Property->GetName(), *Value->GetName()));
				}
				else if (!bSavableWithPrefab)
				{
					OutProblems.Add(FString::Printf(TEXT("%s.%s is not Instance Editable, so its value cannot be saved with the prefab. Tick Instance Editable in the blueprint to keep this binding.")
						, *Comp->GetName(), *Property->GetName()));
				}
				continue;
			}

			// BindWidget-style: null property + name-matching element => bind it now, so the
			// reference is serialized with the prefab and costs nothing at runtime.
			// Not-Instance-Editable variables are deliberately left alone -- the serializer
			// cannot save them, and unticking the flag is the user's opt-out from auto-bind.
			if (!bSavableWithPrefab)continue;
			TArray<AActor*>* MatchedActors = LabelToActors.Find(Property->GetName());
			if (MatchedActors == nullptr)continue;
			if (MatchedActors->Num() > 1)
			{
				OutProblems.Add(FString::Printf(TEXT("%s.%s matches %d elements named \"%s\"; rename them so the binding is unambiguous.")
					, *Comp->GetName(), *Property->GetName(), MatchedActors->Num(), *Property->GetName()));
				continue;
			}
			AActor* MatchedActor = (*MatchedActors)[0];

			UObject* NewValue = nullptr;
			if (Property->PropertyClass->IsChildOf(AActor::StaticClass()))
			{
				if (MatchedActor->GetClass()->IsChildOf(Property->PropertyClass))
				{
					NewValue = MatchedActor;
				}
			}
			else if (Property->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
			{
				NewValue = MatchedActor->FindComponentByClass(TSubclassOf<UActorComponent>(Property->PropertyClass));
			}
			if (NewValue != nullptr)
			{
				Comp->Modify();
				Property->SetObjectPropertyValue_InContainer(Comp, NewValue);
				OutBoundDetails.Add(FString::Printf(TEXT("%s -> %s"), *Property->GetName(), *MatchedActor->GetActorLabel()));
			}
		}
	}
}

}//namespace LGUIPrefabBehaviourUtils

#undef LOCTEXT_NAMESPACE
