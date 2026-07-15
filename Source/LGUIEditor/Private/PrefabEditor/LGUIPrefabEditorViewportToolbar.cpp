// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIPrefabEditorViewportToolbar.h"
#include "LGUIPrefabEditorViewport.h"
#include "Core/LGUISettings.h"
#include "Core/ActorComponent/UIItem.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SLGUIPrefabEditorViewportToolbar"

///////////////////////////////////////////////////////////
// SSpriteEditorViewportToolbar

void SLGUIPrefabEditorViewportToolbar::Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
{
	SCommonEditorViewportToolbarBase::Construct(SCommonEditorViewportToolbarBase::FArguments(), InInfoProvider);
}

void SLGUIPrefabEditorViewportToolbar::ExtendLeftAlignedToolbarSlots(TSharedPtr<SHorizontalBox> MainBoxPtr, TSharedPtr<SViewportToolBar> ParentToolBarPtr) const
{
	if (!MainBoxPtr.IsValid())return;

	// "2D" toggle, Unity-style: switches between the ortho canvas view (LVT_OrthoYZ, facing the
	// UI plane -- arrow-key nudge works there too) and the perspective view. State persists per prefab.
	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(4.0f, 1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.ToolTipText(LOCTEXT("Toggle2DTooltip", "Toggle 2D canvas view (ortho, facing the UI) / 3D perspective view"))
			.IsChecked_Lambda([this]()
				{
					auto Viewport = StaticCastSharedRef<SLGUIPrefabEditorViewport>(GetInfoProvider().GetViewportWidget());
					return Viewport->IsViewport2D() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
			.OnCheckStateChanged_Lambda([this](ECheckBoxState)
				{
					auto Viewport = StaticCastSharedRef<SLGUIPrefabEditorViewport>(GetInfoProvider().GetViewportWidget());
					Viewport->ToggleViewportType2D3D();
				})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Toggle2DLabel", "2D"))
				.Font(FAppStyle::Get().GetFontStyle("SmallFontBold"))
			]
		];

	// canvas resolution preview dropdown (UMG screen-size equivalent): sets the root agent's
	// UIItem width/height, which persists as PrefabDataForPrefabEditor.CanvasSize on Apply
	auto GetRootUIItem = [WeakToolbar = TWeakPtr<const SLGUIPrefabEditorViewportToolbar>(SharedThis(this))]() -> UUIItem*
	{
		if (auto Toolbar = WeakToolbar.Pin())
		{
			auto Viewport = StaticCastSharedRef<SLGUIPrefabEditorViewport>(Toolbar->GetInfoProvider().GetViewportWidget());
			return Viewport->GetRootAgentUIItem();
		}
		return nullptr;
	};
	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(2.0f, 1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ToolTipText(LOCTEXT("ResolutionTooltip", "Canvas preview resolution (sets the root canvas size, persisted with the prefab)"))
			.OnGetMenuContent_Lambda([GetRootUIItem]()
				{
					struct FPreset { const TCHAR* Name; FIntPoint Size; };
					static const FPreset Presets[] =
					{
						{ TEXT("1920 x 1080 (FHD)"), FIntPoint(1920, 1080) },
						{ TEXT("2560 x 1440 (QHD)"), FIntPoint(2560, 1440) },
						{ TEXT("3840 x 2160 (4K)"), FIntPoint(3840, 2160) },
						{ TEXT("1280 x 720 (HD)"), FIntPoint(1280, 720) },
						{ TEXT("1080 x 1920 (Portrait FHD)"), FIntPoint(1080, 1920) },
						{ TEXT("750 x 1334 (Phone Portrait)"), FIntPoint(750, 1334) },
						{ TEXT("768 x 1024 (Tablet Portrait)"), FIntPoint(768, 1024) },
					};
					auto SetSize = [GetRootUIItem](FIntPoint Size)
					{
						if (auto UIItem = GetRootUIItem())
						{
							FScopedTransaction Transaction(LOCTEXT("SetPreviewResolution_Transaction", "LGUI Set Canvas Preview Resolution"));
							UIItem->Modify();
							UIItem->SetWidth(Size.X);
							UIItem->SetHeight(Size.Y);
						}
					};
					FMenuBuilder MenuBuilder(true, nullptr);
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("ResolutionPresets", "Canvas Resolution"));
					for (const auto& Preset : Presets)
					{
						MenuBuilder.AddMenuEntry(
							FText::FromString(Preset.Name),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([SetSize, Size = Preset.Size]() { SetSize(Size); })));
					}
					MenuBuilder.AddSeparator();
					MenuBuilder.AddMenuEntry(
						LOCTEXT("SwapResolution", "Swap Width/Height"),
						LOCTEXT("SwapResolutionTooltip", "Rotate between landscape and portrait"),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([GetRootUIItem, SetSize]()
							{
								if (auto UIItem = GetRootUIItem())
								{
									SetSize(FIntPoint(FMath::RoundToInt(UIItem->GetHeight()), FMath::RoundToInt(UIItem->GetWidth())));
								}
							})));
					MenuBuilder.EndSection();
					return MenuBuilder.MakeWidget();
				})
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.Text_Lambda([GetRootUIItem]()
					{
						if (auto UIItem = GetRootUIItem())
						{
							return FText::FromString(FString::Printf(TEXT("%d x %d")
								, FMath::RoundToInt(UIItem->GetWidth()), FMath::RoundToInt(UIItem->GetHeight())));
						}
						return LOCTEXT("NoCanvas", "Canvas");
					})
			]
		];
}

TSharedRef<SWidget> SLGUIPrefabEditorViewportToolbar::GenerateShowMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();

	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder ShowMenuBuilder(bInShouldCloseWindowAfterMenuSelection, ViewportRef->GetCommandList());
	{
		// the LGUI viewport helpers used to live only in the level editor's "LGUI Tools" dropdown;
		// surface them here where UI authoring actually happens
		ShowMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAnchorTool", "Anchor Tool"),
			LOCTEXT("ShowAnchorToolTooltip", "Draggable anchor/pivot handles on the selected UI element"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([]()
					{
						auto Settings = GetMutableDefault<ULGUIEditorSettings>();
						Settings->bShowAnchorTool = !Settings->bShowAnchorTool;
						Settings->SaveConfig();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([]() { return GetDefault<ULGUIEditorSettings>()->bShowAnchorTool; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		ShowMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowHelperFrame", "Helper Frame"),
			LOCTEXT("ShowHelperFrameTooltip", "Rectangle outlines for the selected UI element and its relatives"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([]()
					{
						auto Settings = GetMutableDefault<ULGUIEditorSettings>();
						Settings->bDrawHelperFrame = !Settings->bDrawHelperFrame;
						Settings->SaveConfig();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([]() { return GetDefault<ULGUIEditorSettings>()->bDrawHelperFrame; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}

	return ShowMenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
