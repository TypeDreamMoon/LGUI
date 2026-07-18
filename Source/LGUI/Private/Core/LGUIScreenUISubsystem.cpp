// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LGUIScreenUISubsystem.h"
#include "LGUI.h"
#include "LGUIBPLibrary.h"
#include "Core/ActorComponent/UIItem.h"
#include "Core/ActorComponent/LGUICanvas.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"

ULGUIScreenUISubsystem* ULGUIScreenUISubsystem::Get(UWorld* InWorld)
{
	return InWorld != nullptr ? InWorld->GetSubsystem<ULGUIScreenUISubsystem>() : nullptr;
}
ULGUIScreenUISubsystem* ULGUIScreenUISubsystem::GetLGUIScreenUISubsystem(UObject* WorldContextObject)
{
	return Get(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull));
}

void ULGUIScreenUISubsystem::ApplySortOrder(AActor* InRoot, int32 InSortOrder)
{
	if (InRoot == nullptr || InSortOrder == 0)return;
	auto Canvas = InRoot->FindComponentByClass<ULGUICanvas>();
	if (Canvas == nullptr)
	{
		Canvas = NewObject<ULGUICanvas>(InRoot);
		InRoot->AddInstanceComponent(Canvas);
		Canvas->RegisterComponent();
	}
	Canvas->SetSortOrder(InSortOrder, true);
}

void ULGUIScreenUISubsystem::DestroyEntry(FEntry& InEntry)
{
	if (AActor* Root = InEntry.Root.Get())
	{
		// LGUI pages are actor hierarchies; plain Destroy would orphan the children
		ULGUIBPLibrary::DestroyActorWithHierarchy(Root, true);
	}
}

FName ULGUIScreenUISubsystem::FindNameForActor(AActor* InRoot)const
{
	if (InRoot == nullptr)return NAME_None;
	for (auto& KeyValue : Entries)
	{
		if (KeyValue.Value.Root.Get() == InRoot)
		{
			return KeyValue.Key;
		}
	}
	return NAME_None;
}

void ULGUIScreenUISubsystem::AddToViewport(AActor* InRoot, int32 InSortOrder)
{
	if (InRoot == nullptr)return;
	// already tracked: just re-apply the sort order, don't double-register
	if (!FindNameForActor(InRoot).IsNone())
	{
		ApplySortOrder(InRoot, InSortOrder);
		return;
	}
	const FName AutoName(*FString::Printf(TEXT("__Viewport_%d"), AutoNameCounter++));
	RegisterUI(AutoName, InRoot, InSortOrder);
}

void ULGUIScreenUISubsystem::RemoveFromViewport(AActor* InRoot)
{
	const FName Name = FindNameForActor(InRoot);
	if (!Name.IsNone())
	{
		RemoveUI(Name);
	}
}

bool ULGUIScreenUISubsystem::IsInViewport(AActor* InRoot)const
{
	return !FindNameForActor(InRoot).IsNone();
}

void ULGUIScreenUISubsystem::RegisterUI(FName InName, AActor* InRoot, int32 InSortOrder)
{
	if (InName.IsNone() || InRoot == nullptr)return;
	if (FEntry* Existing = Entries.Find(InName))
	{
		if (Existing->Root.Get() != InRoot)
		{
			UE_LOG(LGUI, Log, TEXT("[LGUIScreenUI] \"%s\" already registered; the old page is destroyed and replaced."), *InName.ToString());
			DestroyEntry(*Existing);
		}
	}
	ApplySortOrder(InRoot, InSortOrder);
	Entries.Add(InName, FEntry{ InRoot, InSortOrder });
}

AActor* ULGUIScreenUISubsystem::ShowPrefab(FName InName, ULGUIPrefab* InPrefab, int32 InSortOrder)
{
	if (InName.IsNone() || InPrefab == nullptr)return nullptr;
	AActor* Root = ULGUIBPLibrary::LoadPrefabToScreen(GetWorld(), InPrefab, FLGUIPrefab_LoadPrefabCallback(), InSortOrder);
	if (Root != nullptr)
	{
		RegisterUI(InName, Root, 0);//sort order already applied by LoadPrefabToScreen
		Entries[InName].SortOrder = InSortOrder;
	}
	return Root;
}

AActor* ULGUIScreenUISubsystem::GetUI(FName InName)const
{
	if (const FEntry* Entry = Entries.Find(InName))
	{
		return Entry->Root.Get();
	}
	return nullptr;
}

bool ULGUIScreenUISubsystem::IsUIShowing(FName InName)const
{
	AActor* Root = GetUI(InName);
	if (Root == nullptr)return false;
	if (auto RootItem = Cast<UUIItem>(Root->GetRootComponent()))
	{
		return RootItem->GetIsUIActiveSelf();
	}
	return true;
}

void ULGUIScreenUISubsystem::SetUIVisible(FName InName, bool bVisible)
{
	if (AActor* Root = GetUI(InName))
	{
		if (auto RootItem = Cast<UUIItem>(Root->GetRootComponent()))
		{
			RootItem->SetIsUIActive(bVisible);
		}
	}
}

void ULGUIScreenUISubsystem::RemoveUI(FName InName)
{
	FEntry Entry;
	if (Entries.RemoveAndCopyValue(InName, Entry))
	{
		DestroyEntry(Entry);
	}
	Stack.Remove(InName);
}

void ULGUIScreenUISubsystem::RemoveAllUI()
{
	for (auto& KeyValue : Entries)
	{
		DestroyEntry(KeyValue.Value);
	}
	Entries.Reset();
	Stack.Reset();
}

TArray<FName> ULGUIScreenUISubsystem::GetAllUINames()const
{
	TArray<FName> Names;
	Entries.GenerateKeyArray(Names);
	return Names;
}

AActor* ULGUIScreenUISubsystem::PushPrefab(FName InName, ULGUIPrefab* InPrefab)
{
	const int32 SortOrder = StackBaseSortOrder + Stack.Num() * StackSortOrderStep;
	AActor* Root = ShowPrefab(InName, InPrefab, SortOrder);
	if (Root != nullptr)
	{
		Stack.Add(InName);
	}
	return Root;
}

void ULGUIScreenUISubsystem::PushUI(FName InName, AActor* InRoot)
{
	if (InName.IsNone() || InRoot == nullptr)return;
	const int32 SortOrder = StackBaseSortOrder + Stack.Num() * StackSortOrderStep;
	RegisterUI(InName, InRoot, SortOrder);
	Stack.Add(InName);
}

void ULGUIScreenUISubsystem::PopUI()
{
	// skip stale names whose page was destroyed/removed out-of-band
	while (Stack.Num() > 0)
	{
		const FName Top = Stack.Pop();
		FEntry Entry;
		if (Entries.RemoveAndCopyValue(Top, Entry))
		{
			DestroyEntry(Entry);
			return;
		}
	}
}

FName ULGUIScreenUISubsystem::GetTopUI()const
{
	return Stack.Num() > 0 ? Stack.Last() : NAME_None;
}
