// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UIPanelLayoutOverlaySlotCustomization.h"
#include "LGUIEditorUtils.h"
#include "Layout/UIPanelLayout_Overlay.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PanelLayout/HorizontalAlignmentCustomization.h"
#include "PanelLayout/VerticalAlignmentCustomization.h"

#define LOCTEXT_NAMESPACE "UIPanelLayoutOverlaySlotCustomization"

TSharedRef<IDetailCustomization> FUIPanelLayoutOverlaySlotCustomization::MakeInstance()
{
	return MakeShareable(new FUIPanelLayoutOverlaySlotCustomization);
}
void FUIPanelLayoutOverlaySlotCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<UUIPanelLayout_Overlay_Slot>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[UIPanelLayoutOverlaySlotCustomization]Get TargetScript is null"));
		return;
	}
	DetailBuilder.RegisterInstancedCustomPropertyTypeLayout(TEXT("EHorizontalAlignment"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FHorizontalAlignmentCustomization::MakeInstance));
	DetailBuilder.RegisterInstancedCustomPropertyTypeLayout(TEXT("EVerticalAlignment"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FVerticalAlignmentCustomization::MakeInstance));

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Panel Layout Slot");
	Category.AddProperty(GET_MEMBER_NAME_CHECKED(UUIPanelLayout_Overlay_Slot, Padding));
}
#undef LOCTEXT_NAMESPACE
