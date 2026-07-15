// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Layout/UIPanelLayoutBase.h"
#include "LGUI.h"
#include "Core/ActorComponent/UIItem.h"
#include "Core/ActorComponent/UIText.h"
#include "Core/ActorComponent/UISpriteBase.h"
#include "Core/ActorComponent/UITextureBase.h"
#include "Core/LGUISpriteData_BaseObject.h"
#include "Engine/Texture.h"

namespace UIPanelLayoutSlotLocal
{
	/** Natural content size of a UI element, UMG-desired-size style. False when the element has no measurable content. */
	static bool MeasureContentSize(UUIItem* InChild, FVector2D& OutSize)
	{
		if (auto Text = Cast<UUIText>(InChild))
		{
			OutSize = Text->GetTextRealSize();
			return true;
		}
		if (auto Sprite = Cast<UUISpriteBase>(InChild))
		{
			if (auto SpriteData = Sprite->GetSprite())
			{
				auto& SpriteInfo = SpriteData->GetSpriteInfo();
				if (SpriteInfo.GetSourceWidth() > 0 && SpriteInfo.GetSourceHeight() > 0)
				{
					OutSize = FVector2D(SpriteInfo.GetSourceWidth(), SpriteInfo.GetSourceHeight());
					return true;
				}
			}
			return false;
		}
		if (auto Texture = Cast<UUITextureBase>(InChild))
		{
			if (auto TextureObject = Texture->GetTexture())
			{
				const float Width = TextureObject->GetSurfaceWidth();
				const float Height = TextureObject->GetSurfaceHeight();
				if (Width > 0 && Height > 0)
				{
					OutSize = FVector2D(Width, Height);
					return true;
				}
			}
			return false;
		}
		return false;
	}
}

UUIPanelLayoutBase::UUIPanelLayoutBase()
{
}
void UUIPanelLayoutBase::GetLayoutElement(UUIItem* InChild, UObject*& OutLayoutElement, bool& OutIgnoreLayout)const
{
    UUIPanelLayoutSlotBase* LayoutElement = nullptr;
    if (auto LayoutElementPtr = MapChildToSlot.Find(InChild))
    {
        LayoutElement = *LayoutElementPtr;
    }
    else
    {
        LayoutElement = NewObject<UUIPanelLayoutSlotBase>(const_cast<UUIPanelLayoutBase*>(this), GetPanelLayoutSlotClass(), NAME_None, RF_Public | RF_Transactional);
        LayoutElement->SetDesiredSize(FVector2D(InChild->GetWidth(), InChild->GetHeight()));
        // measurable content (text/sprite/texture) starts in UMG mode: the slot follows the
        // content's natural size. Existing slots come from serialized data and keep their mode.
        FVector2D MeasuredSize;
        if (UIPanelLayoutSlotLocal::MeasureContentSize(InChild, MeasuredSize))
        {
            LayoutElement->SetDesiredSizeMode(EUIPanelLayoutSlotDesiredSizeMode::AutoFromContent);
        }
        MapChildToSlot.Add(InChild, LayoutElement);
    }
    OutLayoutElement = LayoutElement;
    OutIgnoreLayout = LayoutElement->GetIgnoreLayout();
}

void UUIPanelLayoutBase::OnUpdateLayout_Implementation()
{
	// AutoFromContent slots follow content that can change without any dimension event
	// (e.g. editing overflow text keeps the UIItem size). The content components cache
	// their measurements, so this per-frame compare is cheap.
	for (auto& KeyValue : MapChildToSlot)
	{
		UUIItem* Child = KeyValue.Key;
		UUIPanelLayoutSlotBase* Slot = KeyValue.Value;
		if (Child == nullptr || Slot == nullptr)continue;
		if (Slot->GetDesiredSizeMode() != EUIPanelLayoutSlotDesiredSizeMode::AutoFromContent)continue;
		FVector2D MeasuredSize;
		if (UIPanelLayoutSlotLocal::MeasureContentSize(Child, MeasuredSize)
			&& Slot->UpdateMeasuredSizeCache(MeasuredSize))
		{
			MarkNeedRebuildLayout();
		}
	}
	Super::OnUpdateLayout_Implementation();
}

FVector2D UUIPanelLayoutSlotBase::ComputeDesiredSize(UUIItem* InChild)const
{
	if (DesiredSizeMode == EUIPanelLayoutSlotDesiredSizeMode::AutoFromContent && InChild != nullptr)
	{
		FVector2D MeasuredSize;
		if (UIPanelLayoutSlotLocal::MeasureContentSize(InChild, MeasuredSize))
		{
			return MeasuredSize;
		}
	}
	return DesiredSize;
}

void UUIPanelLayoutSlotBase::SetDesiredSizeMode(EUIPanelLayoutSlotDesiredSizeMode Value)
{
	if (DesiredSizeMode != Value)
	{
		DesiredSizeMode = Value;
		if (auto Layout = GetTypedOuter<UUIPanelLayoutBase>())
		{
			Layout->MarkNeedRebuildLayout();
		}
	}
}
void UUIPanelLayoutBase::RebuildChildrenList()const
{
    Super::RebuildChildrenList();
    const_cast<UUIPanelLayoutBase*>(this)->CleanMapChildToSlot();
}
void UUIPanelLayoutBase::OnUIChildAcitveInHierarchy(UUIItem* InChild, bool InUIActive)
{
    Super::OnUIChildAcitveInHierarchy(InChild, InUIActive);
}
void UUIPanelLayoutBase::OnUIChildAttachmentChanged(UUIItem* InChild, bool attachOrDetach)
{
    Super::OnUIChildAttachmentChanged(InChild, attachOrDetach);
}
void UUIPanelLayoutBase::OnUIChildHierarchyIndexChanged(UUIItem* InChild)
{
    Super::OnUIChildHierarchyIndexChanged(InChild);
}
void UUIPanelLayoutBase::CleanMapChildToSlot()
{
    //check map if child not exist
    TSet<UUIItem*> KeysToRemove;
    const auto& UIChildren = RootUIComp->GetAttachUIChildren();
    for (auto& KeyValue : MapChildToSlot)
    {
        auto FoundIndex = UIChildren.IndexOfByKey(KeyValue.Key);
        if (FoundIndex == INDEX_NONE)
        {
            KeysToRemove.Add(KeyValue.Key);
        }
    }
    for (auto& Key : KeysToRemove)
    {
        MapChildToSlot.Remove(Key);
    }
}

void UUIPanelLayoutBase::Awake()
{
    Super::Awake();
}
TObjectPtr<UUIPanelLayoutSlotBase> UUIPanelLayoutBase::GetChildSlot(UUIItem* InChild)
{
    if (auto LayoutElementPtr = MapChildToSlot.Find(InChild))
    {
        return *LayoutElementPtr;
    }
    return nullptr;
}
#if WITH_EDITOR
FText UUIPanelLayoutBase::GetCategoryDisplayName()const
{
    return FText::FromString(this->GetClass()->GetName());
}
#endif

#if WITH_EDITOR
void UUIPanelLayoutSlotBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (auto Layout = GetTypedOuter<UUIPanelLayoutBase>())
    {
        Layout->MarkNeedRebuildLayout();
        Layout->MarkNeedRebuildChildrenList();
    }
}
void UUIPanelLayoutSlotBase::PostEditUndo()
{
    Super::PostEditUndo();
    if (auto Layout = GetTypedOuter<UUIPanelLayoutBase>())
    {
        Layout->MarkNeedRebuildLayout();
        Layout->MarkNeedRebuildChildrenList();
    }
}
#endif
void UUIPanelLayoutSlotBase::SetIgnoreLayout(bool Value)
{
    if (bIgnoreLayout != Value)
    {
        bIgnoreLayout = Value;
        if (auto Layout = GetTypedOuter<UUIPanelLayoutBase>())
        {
            Layout->MarkNeedRebuildLayout();
            Layout->MarkNeedRebuildChildrenList();
        }
    }
}
void UUIPanelLayoutSlotBase::SetDesiredSize(const FVector2D& Value)
{
    if (DesiredSize != Value)
    {
        DesiredSize = Value;
        if (auto Layout = GetTypedOuter<UUIPanelLayoutBase>())
        {
            Layout->MarkNeedRebuildLayout();
        }
    }
}

void UUIPanelLayoutWithOverrideOrder::SortChildrenList()const
{
    LayoutUIItemChildrenArray.Sort([](FLayoutChild A, FLayoutChild B) //sort children by HierarchyIndex
        {
            auto ASlot = Cast<UUIPanelLayoutSlotWithOverrideOrder>(A.LayoutInterface);
            auto BSlot = Cast<UUIPanelLayoutSlotWithOverrideOrder>(B.LayoutInterface);
            if (ASlot && BSlot)
            {
                if (ASlot->GetOverrideLayoutOrder() < BSlot->GetOverrideLayoutOrder())
                {
                    return true;
                }
                else if (ASlot->GetOverrideLayoutOrder() > BSlot->GetOverrideLayoutOrder())
                {
                    return false;
                }
                //else use HierarchyIndex
            }
            else
            {
                if (ASlot && ASlot->GetOverrideLayoutOrder() != 0)
                {
                    return ASlot->GetOverrideLayoutOrder() < 0;
                }
                if (BSlot && BSlot->GetOverrideLayoutOrder() != 0)
                {
                    return BSlot->GetOverrideLayoutOrder() < 0;
                }
            }
            if (A.ChildUIItem->GetHierarchyIndex() < B.ChildUIItem->GetHierarchyIndex())
                return true;
            return false;
        });
}
void UUIPanelLayoutSlotWithOverrideOrder::SetOverrideLayoutOrder(int Value)
{
    if (OverrideLayoutOrder != Value)
    {
        OverrideLayoutOrder = Value;
        if (auto Layout = GetTypedOuter<UUIPanelLayoutBase>())
        {
            Layout->MarkNeedRebuildLayout();
            Layout->MarkNeedSortChildrenList();
        }
    }
}
