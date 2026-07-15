// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UIPanelLayoutBase.h"
#include "Components/SlateWrapperTypes.h"
#include "UIPanelLayout_Overlay.generated.h"

/**
 * Stack child elements on top of each other in the same rect, LGUI's counterpart of
 * UMG's Overlay. Each child's slot controls its padding and horizontal/vertical
 * alignment inside this element's rect (Fill stretches the child to the padded rect).
 * Render order is the hierarchy order, same as everywhere else in LGUI.
 */
UCLASS( ClassGroup=(LGUI), meta=(BlueprintSpawnableComponent, DisplayName = "Overlay Layout") )
class LGUI_API UUIPanelLayout_Overlay : public UUIPanelLayoutBase
{
	GENERATED_BODY()
protected:
	/** this object's width set to wrap the widest child (desired size + padding) */
	UPROPERTY(EditAnywhere, Category = "Panel Layout")
		bool bWidthFitToChildren = false;
	/** this object's height set to wrap the tallest child (desired size + padding) */
	UPROPERTY(EditAnywhere, Category = "Panel Layout")
		bool bHeightFitToChildren = false;
public:
	virtual void OnRebuildLayout()override;

	virtual bool GetCanLayoutControlAnchor_Implementation(class UUIItem* InUIItem, FLGUICanLayoutControlAnchor& OutResult)const override;

	virtual UClass* GetPanelLayoutSlotClass()const override;
#if WITH_EDITOR
	virtual FText GetCategoryDisplayName()const override;
#endif
	UFUNCTION(BlueprintCallable, Category = "Panel Layout")
		bool GetWidthFitToChildren()const { return bWidthFitToChildren; }
	UFUNCTION(BlueprintCallable, Category = "Panel Layout")
		bool GetHeightFitToChildren()const { return bHeightFitToChildren; }

	UFUNCTION(BlueprintCallable, Category = "Panel Layout")
		void SetWidthFitToChildren(bool Value);
	UFUNCTION(BlueprintCallable, Category = "Panel Layout")
		void SetHeightFitToChildren(bool Value);
protected:
	virtual void OnUIChildDimensionsChanged(UUIItem* child, bool horizontalPositionChanged, bool verticalPositionChanged, bool widthChanged, bool heightChanged)override;
};

UCLASS(ClassGroup = LGUI, Blueprintable, meta = (DisplayName = "Overlay Slot"))
class LGUI_API UUIPanelLayout_Overlay_Slot : public UUIPanelLayoutSlotBase
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
	void PostEditUndo()override;
#endif
	friend class FUIPanelLayoutOverlaySlotCustomization;
	/** The padding area between the slot and the content it contains. */
	UPROPERTY(EditAnywhere, Category = "Panel Layout Slot")
		FMargin Padding;
	/** The alignment of the object horizontally. */
	UPROPERTY(EditAnywhere, Category = "Panel Layout Slot")
		TEnumAsByte<EHorizontalAlignment> HorizontalAlignment = HAlign_Fill;
	/** The alignment of the object vertically. */
	UPROPERTY(EditAnywhere, Category = "Panel Layout Slot")
		TEnumAsByte<EVerticalAlignment> VerticalAlignment = VAlign_Fill;
public:
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		const FMargin& GetPadding()const { return Padding; }
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		EHorizontalAlignment GetHorizontalAlignment()const { return HorizontalAlignment; }
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		EVerticalAlignment GetVerticalAlignment()const { return VerticalAlignment; }

	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		void SetPadding(const FMargin& Value);
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		void SetHorizontalAlignment(EHorizontalAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		void SetVerticalAlignment(EVerticalAlignment Value);
};
