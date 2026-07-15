// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include <atomic>

class FLGUIPrefabEditor;
class FAssetThumbnailPool;
class ULGUIPrefab;

/** Palette tab groups, UMG-style: element types, behaviour components, prefab assets. */
enum class ELGUIPaletteTab : uint8
{
	Elements = 0,
	Components,
	Prefabs,
};

/**
 * One node of the palette tree: a category header (CategoryName set), a prefab asset row
 * (Asset valid), or a class row (ComponentClass valid -- either a behaviour/effect component
 * class to add to an actor, or a UI element ACTOR class to spawn under it).
 */
struct FLGUIPrefabPaletteItem
{
	FAssetData Asset;
	/** Component class (added to the target actor) OR UI element actor class (spawned under it). */
	TWeakObjectPtr<UClass> ComponentClass;
	FString CategoryName;
	/** True on the pinned "Favorites" header (distinguishes it from a user category literally named "Favorites"). */
	bool bIsFavoritesGroup = false;
	/** True on the fixed component-class group headers (Interaction/Layout/Effect/Behaviour). */
	bool bIsComponentGroup = false;
	TArray<TSharedPtr<FLGUIPrefabPaletteItem>> Children;

	bool IsComponentClass()const { return ComponentClass.IsValid(); }
	bool IsCategory()const { return !Asset.IsValid() && !ComponentClass.IsValid(); }
};

/**
 * "Prefab Palette" tab content for the Prefab Editor, modeled on UMG's Palette:
 * lists every ULGUIPrefab asset in the project grouped by its PaletteCategory,
 * with a search box and thumbnails. Rows are dragged into the viewport as a
 * standard FAssetDragDropOp, which the existing viewport drop path already
 * handles (attach under the selected actor as a sub prefab).
 */
class SLGUIPrefabPalette : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLGUIPrefabPalette) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FLGUIPrefabEditor> InPrefabEditor);
	virtual ~SLGUIPrefabPalette();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	typedef TSharedPtr<FLGUIPrefabPaletteItem> FItemPtr;

	// tree + data
	void RebuildList();
	FString GetCategoryForAsset(const FAssetData& InAssetData)const;
	TSharedRef<ITableRow> OnGenerateRow(FItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(FItemPtr InItem, TArray<FItemPtr>& OutChildren);
	void OnItemDoubleClick(FItemPtr InItem);
	TSharedPtr<SWidget> OnContextMenuOpening();

	// search
	void OnSearchTextChanged(const FText& InText);

	// drag
	FReply OnItemDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FItemPtr InItem);

	// context menu actions
	void BrowseToAsset(FItemPtr InItem);
	void SetItemCategory(FItemPtr InItem, FString NewCategory);
	void OnNewCategoryTextCommitted(const FText& InText, ETextCommit::Type CommitType, FItemPtr InItem);

	// favorites (persisted per project in the editor ini)
	bool IsFavorite(const FAssetData& InAssetData)const;
	void ToggleFavorite(FItemPtr InItem);
	void LoadFavorites();
	void SaveFavorites()const;

	// hide-in-palette (stored on the prefab asset, ULGUIPrefab::bHideInPalette)
	bool IsAssetHiddenInPalette(const FAssetData& InAssetData)const;
	void ToggleItemHidden(FItemPtr InItem);

	// component class rows
	void CollectComponentClassGroups(TArray<FItemPtr>& OutGroupHeaders, FItemPtr& InOutFavoritesHeader);
	void AddComponentToSelectedActor(FItemPtr InItem);
	/** Favorites key: object path for assets, class path for component classes. */
	FString GetItemFavoriteKey(FItemPtr InItem)const;

	// tab groups
	void CollectElementGroups(TArray<FItemPtr>& OutGroupHeaders, FItemPtr& InOutFavoritesHeader);
	void CollectPrefabCategories(TArray<FItemPtr>& OutHeaders, FItemPtr& InOutFavoritesHeader);
	void AddToFavoritesHeaderIfFavorite(const FItemPtr& InItem, FItemPtr& InOutFavoritesHeader);
	void SetCurrentTab(ELGUIPaletteTab InTab);

	// asset registry change handlers (may fire off the game thread -- only set the dirty flag)
	void OnAssetChanged(const FAssetData& InAssetData);
	void OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath);
	void OnAssetRegistryFilesLoaded();
	void RequestRebuild() { bPendingRebuild = true; }

private:
	TWeakPtr<FLGUIPrefabEditor> PrefabEditorPtr;

	TSharedPtr<STreeView<FItemPtr>> TreeView;
	TArray<FItemPtr> RootItems;
	/** All category names currently present (unfiltered), for the "Set Category" context submenu. */
	TArray<FString> KnownCategories;

	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FTextFilterExpressionEvaluator SearchFilter{ ETextFilterExpressionEvaluatorMode::BasicString };
	/** Object paths of favorited prefab assets. */
	TSet<FString> FavoritePaths;
	/** When true, prefabs marked bHideInPalette are shown (greyed out) so they can be unhidden. */
	bool bShowHiddenPrefabs = false;
	/** Active tab group (persisted in the editor ini). */
	ELGUIPaletteTab CurrentTab = ELGUIPaletteTab::Elements;

	std::atomic<bool> bPendingRebuild{ false };

	FDelegateHandle OnAssetAddedHandle;
	FDelegateHandle OnAssetRemovedHandle;
	FDelegateHandle OnAssetRenamedHandle;
	FDelegateHandle OnAssetUpdatedHandle;
	FDelegateHandle OnFilesLoadedHandle;
};
