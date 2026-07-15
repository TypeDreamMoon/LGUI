// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "SLGUIPrefabPalette.h"
#include "LGUIPrefabEditor.h"
#include "PrefabSystem/LGUIPrefab.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetThumbnail.h"
#include "ContentBrowserModule.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabPalette"

namespace LGUIPrefabPaletteLocal
{
	const FName PaletteCategoryTagName(TEXT("PaletteCategory"));
	const TCHAR* UncategorizedKey = TEXT("");//internal key for prefabs with no category

	FText GetCategoryDisplayText(const FString& InCategory)
	{
		return InCategory.IsEmpty() ? LOCTEXT("Uncategorized", "Uncategorized") : FText::FromString(InCategory);
	}
}

void SLGUIPrefabPalette::Construct(const FArguments& InArgs, TSharedPtr<FLGUIPrefabEditor> InPrefabEditor)
{
	PrefabEditorPtr = InPrefabEditor;
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);//FTickableEditorObject, ticks itself

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2, 2, 2, 4)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search prefabs"))
			.OnTextChanged(this, &SLGUIPrefabPalette::OnSearchTextChanged)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(TreeView, STreeView<FItemPtr>)
			.SelectionMode(ESelectionMode::Single)
			.TreeItemsSource(&RootItems)
			.OnGenerateRow(this, &SLGUIPrefabPalette::OnGenerateRow)
			.OnGetChildren(this, &SLGUIPrefabPalette::OnGetChildren)
			.OnMouseButtonDoubleClick(this, &SLGUIPrefabPalette::OnItemDoubleClick)
			.OnContextMenuOpening(this, &SLGUIPrefabPalette::OnContextMenuOpening)
		]
	];

	// keep the list in sync with the project's assets; handlers may fire off the game
	// thread during scans, so they only set a flag consumed in Tick
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	OnAssetAddedHandle = AssetRegistry.OnAssetAdded().AddSP(this, &SLGUIPrefabPalette::OnAssetChanged);
	OnAssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddSP(this, &SLGUIPrefabPalette::OnAssetChanged);
	OnAssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddSP(this, &SLGUIPrefabPalette::OnAssetRenamed);
	OnAssetUpdatedHandle = AssetRegistry.OnAssetUpdated().AddSP(this, &SLGUIPrefabPalette::OnAssetChanged);
	OnFilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddSP(this, &SLGUIPrefabPalette::OnAssetRegistryFilesLoaded);

	RebuildList();
}

SLGUIPrefabPalette::~SLGUIPrefabPalette()
{
	// registry may already be torn down at editor shutdown
	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		AssetRegistry->OnAssetAdded().Remove(OnAssetAddedHandle);
		AssetRegistry->OnAssetRemoved().Remove(OnAssetRemovedHandle);
		AssetRegistry->OnAssetRenamed().Remove(OnAssetRenamedHandle);
		AssetRegistry->OnAssetUpdated().Remove(OnAssetUpdatedHandle);
		AssetRegistry->OnFilesLoaded().Remove(OnFilesLoadedHandle);
	}
}

void SLGUIPrefabPalette::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bPendingRebuild.exchange(false))
	{
		RebuildList();
	}
}

FString SLGUIPrefabPalette::GetCategoryForAsset(const FAssetData& InAssetData)const
{
	// loaded assets may have an unsaved category edit -- prefer the live property
	// (IsAssetLoaded guarantees GetAsset() won't trigger a load)
	if (InAssetData.IsAssetLoaded())
	{
		if (auto Prefab = Cast<ULGUIPrefab>(InAssetData.GetAsset()))
		{
#if WITH_EDITORONLY_DATA
			return Prefab->PaletteCategory;
#endif
		}
	}
	return InAssetData.GetTagValueRef<FString>(LGUIPrefabPaletteLocal::PaletteCategoryTagName);
}

void SLGUIPrefabPalette::RebuildList()
{
	RootItems.Reset();
	KnownCategories.Reset();

	TArray<FAssetData> PrefabAssets;
	IAssetRegistry::GetChecked().GetAssetsByClass(ULGUIPrefab::StaticClass()->GetClassPathName(), PrefabAssets, true);

	FSoftObjectPath EditingPrefabPath;
	if (auto PrefabEditor = PrefabEditorPtr.Pin())
	{
		if (auto EditingPrefab = PrefabEditor->GetPrefabBeingEdited())
		{
			EditingPrefabPath = FSoftObjectPath(EditingPrefab);
		}
	}

	const bool bFilterActive = !SearchFilter.GetFilterText().IsEmpty();
	TMap<FString, FItemPtr> CategoryMap;
	for (auto& AssetData : PrefabAssets)
	{
		// the prefab being edited can never be its own child -- hide it
		if (EditingPrefabPath.IsValid() && AssetData.ToSoftObjectPath() == EditingPrefabPath)
		{
			continue;
		}

		const FString Category = GetCategoryForAsset(AssetData);
		KnownCategories.AddUnique(Category);

		if (bFilterActive && !SearchFilter.TestTextFilter(FBasicStringFilterExpressionContext(AssetData.AssetName.ToString())))
		{
			continue;
		}

		FItemPtr& Header = CategoryMap.FindOrAdd(Category);
		if (!Header.IsValid())
		{
			Header = MakeShared<FLGUIPrefabPaletteItem>();
			Header->CategoryName = Category;
		}
		auto Item = MakeShared<FLGUIPrefabPaletteItem>();
		Item->Asset = AssetData;
		Header->Children.Add(Item);
	}

	CategoryMap.GenerateValueArray(RootItems);
	// alphabetical categories, "Uncategorized" (empty) pinned to the bottom
	RootItems.Sort([](const FItemPtr& A, const FItemPtr& B) {
		if (A->CategoryName.IsEmpty() != B->CategoryName.IsEmpty())
		{
			return B->CategoryName.IsEmpty();
		}
		return A->CategoryName < B->CategoryName;
		});
	for (auto& Header : RootItems)
	{
		Header->Children.Sort([](const FItemPtr& A, const FItemPtr& B) {
			return A->Asset.AssetName.LexicalLess(B->Asset.AssetName);
			});
	}
	KnownCategories.Sort();

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		for (auto& Header : RootItems)
		{
			TreeView->SetItemExpansion(Header, true);
		}
	}
}

TSharedRef<ITableRow> SLGUIPrefabPalette::OnGenerateRow(FItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (InItem->IsCategory())
	{
		return SNew(STableRow<FItemPtr>, OwnerTable)
			.ShowSelection(false)
			.Padding(FMargin(4, 3))
			[
				SNew(STextBlock)
				.Font(FAppStyle::Get().GetFontStyle("SmallFontBold"))
				.Text(LGUIPrefabPaletteLocal::GetCategoryDisplayText(InItem->CategoryName).ToUpper())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

	auto Thumbnail = MakeShared<FAssetThumbnail>(InItem->Asset, 32, 32, ThumbnailPool);
	return SNew(STableRow<FItemPtr>, OwnerTable)
		.Padding(FMargin(2, 1))
		.OnDragDetected(FOnDragDetected::CreateSP(this, &SLGUIPrefabPalette::OnItemDragDetected, InItem))
		.ToolTipText(FText::Format(LOCTEXT("PrefabRowTooltip", "{0}\nDrag into the viewport to add as sub prefab (attaches under the selected actor).")
			, FText::FromString(InItem->Asset.GetSoftObjectPath().ToString())))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				SNew(SBox)
				.WidthOverride(32)
				.HeightOverride(32)
				[
					Thumbnail->MakeThumbnailWidget()
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromName(InItem->Asset.AssetName))
			]
		];
}

void SLGUIPrefabPalette::OnGetChildren(FItemPtr InItem, TArray<FItemPtr>& OutChildren)
{
	OutChildren = InItem->Children;
}

void SLGUIPrefabPalette::OnSearchTextChanged(const FText& InText)
{
	SearchFilter.SetFilterText(InText);
	RequestRebuild();
}

FReply SLGUIPrefabPalette::OnItemDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FItemPtr InItem)
{
	if (InItem.IsValid() && !InItem->IsCategory())
	{
		// standard asset drag: the viewport's existing OnDrop path (TryHandleAssetDragDropOperation)
		// consumes it exactly like a drag from the Content Browser
		return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(InItem->Asset));
	}
	return FReply::Unhandled();
}

void SLGUIPrefabPalette::OnItemDoubleClick(FItemPtr InItem)
{
	if (InItem.IsValid() && !InItem->IsCategory())
	{
		BrowseToAsset(InItem);
	}
	else if (InItem.IsValid() && TreeView.IsValid())
	{
		TreeView->SetItemExpansion(InItem, !TreeView->IsItemExpanded(InItem));
	}
}

TSharedPtr<SWidget> SLGUIPrefabPalette::OnContextMenuOpening()
{
	auto SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.Num() != 1 || SelectedItems[0]->IsCategory())
	{
		return nullptr;
	}
	FItemPtr Item = SelectedItems[0];

	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("PrefabSection", "Prefab"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("BrowseToAsset", "Browse to Asset"),
			LOCTEXT("BrowseToAssetTooltip", "Select this prefab in the Content Browser"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SystemWideCommands.FindInContentBrowser"),
			FUIAction(FExecuteAction::CreateSP(this, &SLGUIPrefabPalette::BrowseToAsset, Item)));

		MenuBuilder.AddSubMenu(
			LOCTEXT("SetCategory", "Set Category"),
			LOCTEXT("SetCategoryTooltip", "Set this prefab's palette category (stored on the prefab asset)"),
			FNewMenuDelegate::CreateLambda([this, Item](FMenuBuilder& SubMenuBuilder)
				{
					for (auto& Category : KnownCategories)
					{
						SubMenuBuilder.AddMenuEntry(
							LGUIPrefabPaletteLocal::GetCategoryDisplayText(Category),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateSP(this, &SLGUIPrefabPalette::SetItemCategory, Item, Category)));
					}
					SubMenuBuilder.AddSeparator();
					SubMenuBuilder.AddWidget(
						SNew(SBox)
						.Padding(FMargin(8, 2))
						.MinDesiredWidth(150)
						[
							SNew(SEditableTextBox)
							.HintText(LOCTEXT("NewCategoryHint", "New category..."))
							.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SLGUIPrefabPalette::OnNewCategoryTextCommitted, Item))
						],
						FText::GetEmpty(), true, false);
				}));
	}
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

void SLGUIPrefabPalette::BrowseToAsset(FItemPtr InItem)
{
	if (InItem.IsValid() && !InItem->IsCategory())
	{
		auto& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(TArray<FAssetData>{ InItem->Asset });
	}
}

void SLGUIPrefabPalette::SetItemCategory(FItemPtr InItem, FString NewCategory)
{
	if (!InItem.IsValid() || InItem->IsCategory())return;
	// explicit user action -- loading the single asset is acceptable here
	if (auto Prefab = Cast<ULGUIPrefab>(InItem->Asset.GetAsset()))
	{
#if WITH_EDITORONLY_DATA
		if (Prefab->PaletteCategory != NewCategory)
		{
			FScopedTransaction Transaction(LOCTEXT("SetPaletteCategoryTransaction", "Set Prefab Palette Category"));
			Prefab->Modify();
			Prefab->PaletteCategory = NewCategory;
			Prefab->MarkPackageDirty();
		}
#endif
		RequestRebuild();
	}
}

void SLGUIPrefabPalette::OnNewCategoryTextCommitted(const FText& InText, ETextCommit::Type CommitType, FItemPtr InItem)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FSlateApplication::Get().DismissAllMenus();
		SetItemCategory(InItem, InText.ToString().TrimStartAndEnd());
	}
}

void SLGUIPrefabPalette::OnAssetChanged(const FAssetData& InAssetData)
{
	// cheap class check; fires for every asset in the project
	if (InAssetData.AssetClassPath == ULGUIPrefab::StaticClass()->GetClassPathName())
	{
		RequestRebuild();
	}
}

void SLGUIPrefabPalette::OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath)
{
	OnAssetChanged(InAssetData);
}

void SLGUIPrefabPalette::OnAssetRegistryFilesLoaded()
{
	RequestRebuild();
}

#undef LOCTEXT_NAMESPACE
