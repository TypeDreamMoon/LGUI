// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIPrefabEditorViewportToolbar.h"
#include "LGUIPrefabEditorViewport.h"
#include "Core/LGUISettings.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SCheckBox.h"
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
