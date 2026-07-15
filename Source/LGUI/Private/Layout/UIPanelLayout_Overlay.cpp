// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Layout/UIPanelLayout_Overlay.h"
#include "LGUI.h"
#include "Core/ActorComponent/UIItem.h"

DECLARE_CYCLE_STAT(TEXT("UIPanelLayout Overlay RebuildLayout"), STAT_PanelLayout_Overlay, STATGROUP_LGUI);

void UUIPanelLayout_Overlay::OnUIChildDimensionsChanged(UUIItem* child, bool horizontalPositionChanged, bool verticalPositionChanged, bool widthChanged, bool heightChanged)
{
    //skip UILayoutBase
    Super::Super::OnUIChildDimensionsChanged(child, horizontalPositionChanged, verticalPositionChanged, widthChanged, heightChanged);
    if (this->GetWorld() == nullptr)return;
    if (child->GetIsUIActiveInHierarchy())
    {
        MarkNeedRebuildLayout();
    }
}

void UUIPanelLayout_Overlay::OnRebuildLayout()
{
    SCOPE_CYCLE_COUNTER(STAT_PanelLayout_Overlay);
    if (!CheckRootUIComponent())return;
    if (!GetEnable())return;
	if (bIsAnimationPlaying)
	{
		bShouldRebuildLayoutAfterAnimation = true;
		return;
	}
	CancelAllAnimations();

    EUILayoutAnimationType TempAnimationType = AnimationType;
#if WITH_EDITOR
    if (!this->GetWorld()->IsGameWorld())
    {
        TempAnimationType = EUILayoutAnimationType::Immediately;
    }
#endif

    FVector2D RectSize(RootUIComp->GetWidth(), RootUIComp->GetHeight());
    auto& LayoutChildrenList = GetLayoutUIItemChildren();
    // fit-to-children: an overlay wraps its largest child (desired size + padding)
    if (bWidthFitToChildren || bHeightFitToChildren)
    {
        float ChildWidthMax = 0, ChildHeightMax = 0;
        for (int i = 0; i < LayoutChildrenList.Num(); i++)
        {
            auto& LayoutChild = LayoutChildrenList[i];
            if (auto Slot = Cast<UUIPanelLayout_Overlay_Slot>(LayoutChild.LayoutInterface.Get()))
            {
                if (Slot->GetIgnoreLayout())continue;
                const FVector2D SlotDesiredSize = Slot->ComputeDesiredSize(LayoutChild.ChildUIItem.Get());
                auto& Padding = Slot->GetPadding();
                ChildWidthMax = FMath::Max(ChildWidthMax, (float)(Padding.Left + Padding.Right + SlotDesiredSize.X));
                ChildHeightMax = FMath::Max(ChildHeightMax, (float)(Padding.Top + Padding.Bottom + SlotDesiredSize.Y));
            }
        }
        if (bWidthFitToChildren)
        {
            RectSize.X = ChildWidthMax;
            ApplyWidthWithAnimation(TempAnimationType, RectSize.X, RootUIComp.Get());
        }
        if (bHeightFitToChildren)
        {
            RectSize.Y = ChildHeightMax;
            ApplyHeightWithAnimation(TempAnimationType, RectSize.Y, RootUIComp.Get());
        }
    }

    // every child occupies the whole rect (a single-cell grid): padding shrinks the
    // available area, alignment places the child inside it, Fill stretches to it
    for (int i = 0; i < LayoutChildrenList.Num(); i++)
    {
        auto& LayoutChild = LayoutChildrenList[i];
        if (auto Slot = Cast<UUIPanelLayout_Overlay_Slot>(LayoutChild.LayoutInterface.Get()))
        {
            if (Slot->GetIgnoreLayout())continue;
            const FVector2D SlotDesiredSize = Slot->ComputeDesiredSize(LayoutChild.ChildUIItem.Get());
            auto& Padding = Slot->GetPadding();
            auto UIItem = LayoutChild.ChildUIItem.Get();
            float ItemAreaWidth = RectSize.X - (Padding.Left + Padding.Right);
            float ItemAreaHeight = RectSize.Y - (Padding.Top + Padding.Bottom);

            auto HAlign = Slot->GetHorizontalAlignment();
            auto VAlign = Slot->GetVerticalAlignment();
            float ItemWidth = SlotDesiredSize.X;
            float ItemHeight = SlotDesiredSize.Y;
            auto ItemOffsetX = 0.0f;
            auto ItemOffsetY = 0.0f;
            if (ItemWidth < ItemAreaWidth)
            {
                switch (HAlign)
                {
                case HAlign_Left:
                    ItemOffsetX = -(ItemAreaWidth - ItemWidth) * 0.5f;
                    break;
                case HAlign_Center:
                    break;
                case HAlign_Right:
                    ItemOffsetX = (ItemAreaWidth - ItemWidth) * 0.5f;
                    break;
                case HAlign_Fill:
                    ItemWidth = ItemAreaWidth;
                    break;
                }
            }
            else//ItemWidth should smaller than ItemAreaWidth
            {
                ItemWidth = ItemAreaWidth;
            }
            if (ItemHeight < ItemAreaHeight)
            {
                switch (VAlign)
                {
                case VAlign_Top:
                    ItemOffsetY = (ItemAreaHeight - ItemHeight) * 0.5f;
                    break;
                case VAlign_Center:
                    break;
                case VAlign_Bottom:
                    ItemOffsetY = -(ItemAreaHeight - ItemHeight) * 0.5f;
                    break;
                case VAlign_Fill:
                    ItemHeight = ItemAreaHeight;
                    break;
                }
            }
            else//ItemHeight should smaller than ItemAreaHeight
            {
                ItemHeight = ItemAreaHeight;
            }

            auto AnchorMin = UIItem->GetAnchorMin();
            auto AnchorMax = UIItem->GetAnchorMax();
            if (AnchorMin.X != AnchorMax.X)//custom anchor not support
            {
                UIItem->SetHorizontalAnchorMinMax(FVector2D(0, 0), true, true);
            }
            if (AnchorMin.Y != AnchorMax.Y)
            {
                UIItem->SetVerticalAnchorMinMax(FVector2D(1, 1), true, true);
            }
            float AnchorOffsetX = UIItem->GetPivot().X * ItemAreaWidth;
            float AnchorOffsetY = -(1.0f - UIItem->GetPivot().Y) * ItemAreaHeight;
            //padding
            AnchorOffsetX += Padding.Left;
            AnchorOffsetY += -Padding.Top;
            //local offset
            AnchorOffsetX += ItemOffsetX;
            AnchorOffsetY += ItemOffsetY;
            //parent anchor
            AnchorOffsetX -= AnchorMin.X * RootUIComp->GetWidth();
            AnchorOffsetY += (1.0f - AnchorMin.Y) * RootUIComp->GetHeight();//LGUI anchor Y is Unity-style (1 = top): offset from the anchor line UP to the parent top, which the cell math is relative to. Plain AnchorMin.Y only coincides at the 0.5 center anchor.
            ApplyAnchoredPositionWithAnimation(TempAnimationType, FVector2D(AnchorOffsetX, AnchorOffsetY), UIItem);
            ApplyWidthWithAnimation(TempAnimationType, ItemWidth, UIItem);
            ApplyHeightWithAnimation(TempAnimationType, ItemHeight, UIItem);
        }
    }

	if (TempAnimationType == EUILayoutAnimationType::EaseAnimation)
	{
		EndSetupAnimations();
	}
}

bool UUIPanelLayout_Overlay::GetCanLayoutControlAnchor_Implementation(class UUIItem* InUIItem, FLGUICanLayoutControlAnchor& OutResult)const
{
    if (this->GetRootUIComponent() == InUIItem)//self
    {
        OutResult.bCanControlHorizontalSizeDelta = bWidthFitToChildren && this->GetEnable();
        OutResult.bCanControlVerticalSizeDelta = bHeightFitToChildren && this->GetEnable();
        return true;
    }
    else if (this->GetRootUIComponent() == InUIItem->GetAttachParent())//child
    {
        UObject* LayoutElementInterface = nullptr;
        bool bIgnoreLayout = false;
        GetLayoutElement(InUIItem, LayoutElementInterface, bIgnoreLayout);
        if (bIgnoreLayout)
        {
            return true;
        }
        auto Slot = Cast<UUIPanelLayout_Overlay_Slot>(LayoutElementInterface);
        if (!Slot)return false;

        OutResult.bCanControlHorizontalAnchor = this->GetEnable();
        OutResult.bCanControlVerticalAnchor = this->GetEnable();
        OutResult.bCanControlHorizontalAnchoredPosition = this->GetEnable();
        OutResult.bCanControlVerticalAnchoredPosition = this->GetEnable();
        OutResult.bCanControlHorizontalSizeDelta = this->GetEnable();
        OutResult.bCanControlVerticalSizeDelta = this->GetEnable();
        return true;
    }
    return false;
}

UClass* UUIPanelLayout_Overlay::GetPanelLayoutSlotClass()const
{
    return UUIPanelLayout_Overlay_Slot::StaticClass();
}

#if WITH_EDITOR
FText UUIPanelLayout_Overlay::GetCategoryDisplayName()const
{
    return NSLOCTEXT("UIPanelLayout_Overlay", "CategoryDisplayName", "Overlay");
}
#endif
void UUIPanelLayout_Overlay::SetWidthFitToChildren(bool Value)
{
    if (bWidthFitToChildren != Value)
    {
        bWidthFitToChildren = Value;
        MarkNeedRebuildLayout();
    }
}
void UUIPanelLayout_Overlay::SetHeightFitToChildren(bool Value)
{
    if (bHeightFitToChildren != Value)
    {
        bHeightFitToChildren = Value;
        MarkNeedRebuildLayout();
    }
}

#if WITH_EDITOR
void UUIPanelLayout_Overlay_Slot::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (auto Layout = GetTypedOuter<UUIPanelLayout_Overlay>())
    {
        Layout->MarkNeedRebuildLayout();
        Layout->MarkNeedRebuildChildrenList();
    }
}
void UUIPanelLayout_Overlay_Slot::PostEditUndo()
{
    Super::PostEditUndo();
    if (auto Layout = GetTypedOuter<UUIPanelLayout_Overlay>())
    {
        Layout->MarkNeedRebuildLayout();
        Layout->MarkNeedRebuildChildrenList();
    }
}
#endif
void UUIPanelLayout_Overlay_Slot::SetPadding(const FMargin& Value)
{
    if (Padding != Value)
    {
        Padding = Value;
        if (auto Layout = GetTypedOuter<UUIPanelLayout_Overlay>())
        {
            Layout->MarkNeedRebuildLayout();
        }
    }
}
void UUIPanelLayout_Overlay_Slot::SetHorizontalAlignment(EHorizontalAlignment Value)
{
    if (HorizontalAlignment != Value)
    {
        HorizontalAlignment = Value;
        if (auto Layout = GetTypedOuter<UUIPanelLayout_Overlay>())
        {
            Layout->MarkNeedRebuildLayout();
        }
    }
}
void UUIPanelLayout_Overlay_Slot::SetVerticalAlignment(EVerticalAlignment Value)
{
    if (VerticalAlignment != Value)
    {
        VerticalAlignment = Value;
        if (auto Layout = GetTypedOuter<UUIPanelLayout_Overlay>())
        {
            Layout->MarkNeedRebuildLayout();
        }
    }
}
