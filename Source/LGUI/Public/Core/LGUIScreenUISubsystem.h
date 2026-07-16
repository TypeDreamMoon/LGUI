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

	/**
	 * Register a screen UI root actor under a name (e.g. a LGUIBuilder BuildToScreen result).
	 * An existing entry with the same name is destroyed and replaced.
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

	/** Stack pages layer from here upward, above ordinary registered pages. */
	static constexpr int32 StackBaseSortOrder = 1000;
	static constexpr int32 StackSortOrderStep = 10;

	void ApplySortOrder(AActor* InRoot, int32 InSortOrder);
	void DestroyEntry(FEntry& InEntry);
};
