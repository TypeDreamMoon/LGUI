// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LGUIScreenUISubsystem.generated.h"

class ULGUIPrefab;

/**
 * Central registry for screen-space LGUI, the counterpart of UMG's viewport widget
 * management (plus a light menu stack in the spirit of CommonUI):
 *
 * - AddToViewport / RemoveFromViewport: the plain UMG flow -- hand it a built root, it is
 *   shown and managed, no name required (the builder's .AddToViewport() does this for you)
 * - named pages: ShowPrefab / RegisterUI, then GetUI / SetUIVisible / RemoveUI by name
 *   from anywhere, instead of every system keeping its own actor pointers
 * - RemoveAllUI = UMG's RemoveAllWidgets
 * - a sort-order-managed stack: PushPrefab always layers above everything registered,
 *   PopUI removes the top -- back-button menu flows without manual ZOrder bookkeeping
 *
 * Lives per world (game/PIE only), so level travel tears everything down with the world,
 * same as viewport widgets. Visibility uses the root UIItem's IsUIActive (LGUI's SetActive).
 */
UCLASS()
class LGUI_API ULGUIScreenUISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	static ULGUIScreenUISubsystem* Get(UWorld* InWorld);
	/** Blueprint accessor: the screen UI subsystem for this context's world. */
	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get LGUI Screen UI Subsystem"), Category = "LGUI")
		static ULGUIScreenUISubsystem* GetLGUIScreenUISubsystem(UObject* WorldContextObject);

	//--- UMG AddToViewport / RemoveFromParent parity: no name required ---
	/**
	 * Track an already-on-screen UI root (a LGUIBuilder result or a LoadPrefabToScreen'd
	 * actor) so the subsystem manages its lifetime -- UMG's AddToViewport. No name needed;
	 * tracked pages are torn down by RemoveAllUI and by level travel, like viewport widgets.
	 * Re-adding an already-tracked root just re-applies the sort order.
	 * @param InSortOrder When not 0, applied as the root's own canvas layer sort order.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void AddToViewport(AActor* InRoot, int32 InSortOrder = 0);
	/** Destroy the page and stop tracking it -- UMG's RemoveFromParent. No-op if not tracked. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void RemoveFromViewport(AActor* InRoot);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		bool IsInViewport(AActor* InRoot)const;

	/**
	 * Register a screen UI root actor under a name (e.g. a LGUIBuilder BuildToScreen result).
	 * An existing entry with the same name is destroyed and replaced. Use this over
	 * AddToViewport when you want to look the page up by name later.
	 * @param InSortOrder When not 0, applied as the root's own canvas layer sort order.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void RegisterUI(FName InName, AActor* InRoot, int32 InSortOrder = 0);
	/** LoadPrefabToScreen + register: one call from asset to managed screen page. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		AActor* ShowPrefab(FName InName, ULGUIPrefab* InPrefab, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		AActor* GetUI(FName InName)const;
	/** Registered, alive, and its root UIItem is active. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		bool IsUIShowing(FName InName)const;
	/** Show/hide without destroying (root UIItem SetIsUIActive). */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetUIVisible(FName InName, bool bVisible);
	/** Destroy the page (whole actor hierarchy) and unregister it. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void RemoveUI(FName InName);
	/** Destroy every registered page -- UMG's RemoveAllWidgets counterpart. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void RemoveAllUI();
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		TArray<FName> GetAllUINames()const;

	//--- menu stack: sort orders are managed so the top of the stack is always on top ---
	/** Show a prefab layered above everything else and push it on the stack. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		AActor* PushPrefab(FName InName, ULGUIPrefab* InPrefab);
	/** Push an already-built root (e.g. a LGUIBuilder result) on the stack. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void PushUI(FName InName, AActor* InRoot);
	/** Destroy and pop the top of the stack. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void PopUI();
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		FName GetTopUI()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		int32 GetStackDepth()const { return Stack.Num(); }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType)const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

private:
	struct FEntry
	{
		TWeakObjectPtr<AActor> Root;
		int32 SortOrder = 0;
	};
	/** Weak roots: a page destroyed behind our back just drops out on the next access. */
	TMap<FName, FEntry> Entries;
	TArray<FName> Stack;
	/** Auto-name counter for nameless AddToViewport pages. */
	int32 AutoNameCounter = 0;

	/** Registered name of the entry whose root == InRoot, or NAME_None. */
	FName FindNameForActor(AActor* InRoot)const;

	/** Stack pages layer from here upward, above ordinary registered pages. */
	static constexpr int32 StackBaseSortOrder = 1000;
	static constexpr int32 StackSortOrderStep = 10;

	void ApplySortOrder(AActor* InRoot, int32 InSortOrder);
	void DestroyEntry(FEntry& InEntry);
};
