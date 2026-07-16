// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UILayoutWithChildren.h"
#include "UIPanelLayoutBase.generated.h"

class UUIPanelLayoutSlotBase;
class UUIPanelLayoutElement;

UCLASS(Abstract)
class LGUI_API UUIPanelLayoutBase : public UUILayoutWithChildren
{
	GENERATED_BODY()
public:
	UUIPanelLayoutBase();
protected:
	virtual void GetLayoutElement(UUIItem* InChild, UObject*& OutLayoutElement, bool& OutIgnoreLayout)const;
	virtual void RebuildChildrenList()const override;
	virtual void Awake() override;
	/** Per-frame: re-measure AutoFromContent slots (content can change without a dimension event, e.g. text edits) and rebuild when a measurement changed. */
	virtual void OnUpdateLayout_Implementation()override;

	virtual void OnUIChildAcitveInHierarchy(UUIItem* InChild, bool InUIActive)override;
	virtual void OnUIChildAttachmentChanged(UUIItem* InChild, bool attachOrDetach)override;
	virtual void OnUIChildHierarchyIndexChanged(UUIItem* InChild)override;

	void CleanMapChildToSlot();

	UPROPERTY(VisibleAnywhere, Instanced, Category = "Panel Layout", AdvancedDisplay)
		mutable TMap<TObjectPtr<UUIItem>, TObjectPtr<UUIPanelLayoutSlotBase>> MapChildToSlot;
public:
	TObjectPtr<UUIPanelLayoutSlotBase> GetChildSlot(UUIItem* InChild);
	/**
	 * Get the slot for InChild, creating it if this layout has not seen the child yet --
	 * GetChildSlot returns null until the first layout rebuild, which makes configuring a
	 * slot right after attaching a child from code (UI builder, blueprint, script) impossible.
	 * InChild must be an attached UI child of this layout's root.
	 */
	UFUNCTION(BlueprintCallable, Category = "Panel Layout")
		UUIPanelLayoutSlotBase* GetOrCreateChildSlot(UUIItem* InChild);
	virtual UClass* GetPanelLayoutSlotClass()const PURE_VIRTUAL(UUIPanelLayoutBase::GeneratePanelLayoutSlot, return nullptr;);
#if WITH_EDITOR
	/** Return category name for editor display */
	virtual FText GetCategoryDisplayName()const;
	enum class EMoveChildDirectionType
	{
		Left, Right, Top, Bottom,
	};
	virtual bool CanMoveChildToCell(UUIItem* InChild, EMoveChildDirectionType InDirection)const { return false; }
	virtual void MoveChildToCell(UUIItem* InChild, EMoveChildDirectionType InDirection) {}
#endif
};

UENUM(BlueprintType)
enum class EUIPanelLayoutSlotDesiredSizeMode : uint8
{
	/** Use the DesiredSize values entered on this slot. */
	Manual,
	/**
	 * Measure the child's content on every layout rebuild, like UMG's desired size:
	 * UIText = text real size, UISprite = sprite source size, UITexture = texture size.
	 * Falls back to the manual values when the content is not measurable (e.g. a container).
	 */
	AutoFromContent,
};

UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced)
class LGUI_API UUIPanelLayoutSlotBase : public UObject
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
	void PostEditUndo()override;
#endif
	/** How the desired size is determined: entered manually, or measured from the child's content (UMG-style). */
	UPROPERTY(EditAnywhere, Category = "Panel Layout Slot")
		EUIPanelLayoutSlotDesiredSizeMode DesiredSizeMode = EUIPanelLayoutSlotDesiredSizeMode::Manual;
	/** The desired with and height that this layout element trying to fit. */
	UPROPERTY(EditAnywhere, Category = "Panel Layout Slot", meta = (EditCondition = "DesiredSizeMode == EUIPanelLayoutSlotDesiredSizeMode::Manual"))
		FVector2D DesiredSize = FVector2D(100, 100);
	/** Ignore parent layout. */
	UPROPERTY(EditAnywhere, Category = "Panel Layout Slot")
		bool bIgnoreLayout = false;
public:
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		FVector2D GetDesiredSize()const { return DesiredSize; }
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		EUIPanelLayoutSlotDesiredSizeMode GetDesiredSizeMode()const { return DesiredSizeMode; }
	/**
	 * Desired size resolved per DesiredSizeMode: AutoFromContent measures InChild's content
	 * (text / sprite / texture natural size) and falls back to the manual DesiredSize when
	 * the content cannot be measured. This is what the panel layouts actually use.
	 */
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		FVector2D ComputeDesiredSize(class UUIItem* InChild)const;
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		bool GetIgnoreLayout()const { return bIgnoreLayout; }
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		void SetIgnoreLayout(bool Value);
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		void SetDesiredSize(const FVector2D& Value);
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		void SetDesiredSizeMode(EUIPanelLayoutSlotDesiredSizeMode Value);

	/** Change detection for the per-frame AutoFromContent re-measure; stores the value, returns whether it differs from the last call. */
	bool UpdateMeasuredSizeCache(const FVector2D& InMeasured)
	{
		const bool bChanged = !LastMeasuredSize.IsSet() || !LastMeasuredSize.GetValue().Equals(InMeasured);
		LastMeasuredSize = InMeasured;
		return bChanged;
	}
private:
	/** Last AutoFromContent measurement (transient; only for change detection). */
	TOptional<FVector2D> LastMeasuredSize;
};

UCLASS(Abstract)
class LGUI_API UUIPanelLayoutWithOverrideOrder : public UUIPanelLayoutBase
{
	GENERATED_BODY()
public:
	virtual void SortChildrenList()const override;
};

UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced)
class LGUI_API UUIPanelLayoutSlotWithOverrideOrder : public UUIPanelLayoutSlotBase
{
	GENERATED_BODY()
protected:
	/**
	 * Use this value as layout order instead of HierarchyIndex. Lower value will go left-top position.
	 * If 0 then use default order.
	 */
	UPROPERTY(EditAnywhere, Category = "Panel Layout Slot")
		int OverrideLayoutOrder = 0;
public:
	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		int GetOverrideLayoutOrder()const { return OverrideLayoutOrder; }

	UFUNCTION(BlueprintCallable, Category = "Panel Layout Slot")
		void SetOverrideLayoutOrder(int Value);
};

