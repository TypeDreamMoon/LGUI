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

/**
 * One node of the palette tree: either a category header (Asset invalid, CategoryName set)
 * or a prefab asset row (Asset valid).
 */
struct FLGUIPrefabPaletteItem
{
	FAssetData Asset;
	FString CategoryName;
	TArray<TSharedPtr<FLGUIPrefabPaletteItem>> Children;

	bool IsCategory()const { return !Asset.IsValid(); }
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

	std::atomic<bool> bPendingRebuild{ false };

	FDelegateHandle OnAssetAddedHandle;
	FDelegateHandle OnAssetRemovedHandle;
	FDelegateHandle OnAssetRenamedHandle;
	FDelegateHandle OnAssetUpdatedHandle;
	FDelegateHandle OnFilesLoadedHandle;
};
