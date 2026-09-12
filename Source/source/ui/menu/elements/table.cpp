#include "../settings/functions.h"

static const float table_border_size = 1.0f;
static const int table_draw_channel_noclip = 2;                    
static const float table_resize_separator_half_thickness = 4.0f;   

inline ImGuiTableFlags table_fix_flags(ImGuiTableFlags flags, ImGuiWindow* outer_window)
{
    if ((flags & ImGuiTableFlags_SizingMask_) == 0)
        flags |= ((flags & ImGuiTableFlags_ScrollX) || (outer_window->Flags & ImGuiWindowFlags_AlwaysAutoResize)) ? ImGuiTableFlags_SizingFixedFit : ImGuiTableFlags_SizingStretchSame;

    if ((flags & ImGuiTableFlags_SizingMask_) == ImGuiTableFlags_SizingFixedSame)
        flags |= ImGuiTableFlags_NoKeepColumnsVisible;

    if (flags & ImGuiTableFlags_Resizable)
        flags |= ImGuiTableFlags_BordersInnerV;

    if (flags & (ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY))
        flags &= ~(ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_NoHostExtendY);

    if (flags & ImGuiTableFlags_NoBordersInBodyUntilResize)
        flags &= ~ImGuiTableFlags_NoBordersInBody;

    if ((flags & (ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable)) == 0)
        flags |= ImGuiTableFlags_NoSavedSettings;

    if (outer_window->RootWindow->Flags & ImGuiWindowFlags_NoSavedSettings)
        flags |= ImGuiTableFlags_NoSavedSettings;

    return flags;
}

bool begin_table_ex(const char* name, ImGuiID id, int columns_count, ImGuiTableFlags flags, const ImVec2& outer_size, float inner_width)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* outer_window = GetCurrentWindow();
    if (outer_window->SkipItems)
        return false;

    IM_ASSERT(columns_count > 0 && columns_count < IMGUI_TABLE_MAX_COLUMNS);
    if (flags & ImGuiTableFlags_ScrollX)
        IM_ASSERT(inner_width >= 0.0f);

    const bool use_child_window = (flags & (ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) != 0;
    const ImVec2 avail_size = GetContentRegionAvail();
    const ImVec2 actual_outer_size = CalcItemSize(outer_size, ImMax(avail_size.x, 1.0f), use_child_window ? ImMax(avail_size.y, 1.0f) : 0.0f);
    const ImRect outer_rect(outer_window->DC.CursorPos, outer_window->DC.CursorPos + actual_outer_size);
    const bool outer_window_is_measuring_size = (outer_window->AutoFitFramesX > 0) || (outer_window->AutoFitFramesY > 0);
    if (use_child_window && IsClippedEx(outer_rect, 0) && !outer_window_is_measuring_size)
    {
        ItemSize(outer_rect);
        ItemAdd(outer_rect, id);
        return false;
    }

    if (g.DebugBreakInTable == id)
        IM_DEBUG_BREAK();

    ImGuiTable* table = g.Tables.GetOrAddByKey(id);

    const int table_idx = g.Tables.GetIndex(table);
    if (++g.TablesTempDataStacked > g.TablesTempData.Size)
        g.TablesTempData.resize(g.TablesTempDataStacked, ImGuiTableTempData());
    ImGuiTableTempData* temp_data = table->TempData = &g.TablesTempData[g.TablesTempDataStacked - 1];
    temp_data->TableIndex = table_idx;
    table->DrawSplitter = &table->TempData->DrawSplitter;
    table->DrawSplitter->Clear();

    table->IsDefaultSizingPolicy = (flags & ImGuiTableFlags_SizingMask_) == 0;
    flags = table_fix_flags(flags, outer_window);

    const int previous_frame_active = table->LastFrameActive;
    const int instance_no = (previous_frame_active != g.FrameCount) ? 0 : table->InstanceCurrent + 1;
    const ImGuiTableFlags previous_flags = table->Flags;
    table->ID = id;
    table->Flags = flags;
    table->LastFrameActive = g.FrameCount;
    table->OuterWindow = table->InnerWindow = outer_window;
    table->ColumnsCount = columns_count;
    table->IsLayoutLocked = false;
    table->InnerWidth = inner_width;
    temp_data->UserOuterSize = outer_size;

    ImGuiID instance_id;
    table->InstanceCurrent = (ImS16)instance_no;
    if (instance_no > 0)
    {
        IM_ASSERT(table->ColumnsCount == columns_count && "BeginTable(): Cannot change columns count mid-frame while preserving same ID");
        if (table->InstanceDataExtra.Size < instance_no)
            table->InstanceDataExtra.push_back(ImGuiTableInstanceData());
        instance_id = GetIDWithSeed(instance_no, GetIDWithSeed("##Instances", NULL, id));
    }
    else
    {
        instance_id = id;
    }
    ImGuiTableInstanceData* table_instance = TableGetInstanceData(table, table->InstanceCurrent);
    table_instance->TableInstanceID = instance_id;

    if (use_child_window)
    {
        ImVec2 override_content_size(FLT_MAX, FLT_MAX);
        if ((flags & ImGuiTableFlags_ScrollX) && !(flags & ImGuiTableFlags_ScrollY))
            override_content_size.y = FLT_MIN;

        if ((flags & ImGuiTableFlags_ScrollX) && inner_width > 0.0f)
            override_content_size.x = inner_width;

        if (override_content_size.x != FLT_MAX || override_content_size.y != FLT_MAX)
            SetNextWindowContentSize(ImVec2(override_content_size.x != FLT_MAX ? override_content_size.x : 0.0f, override_content_size.y != FLT_MAX ? override_content_size.y : 0.0f));

        if ((previous_flags & (ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) == 0)
            SetNextWindowScroll(ImVec2(0.0f, 0.0f));

        ImGuiWindowFlags child_flags = (flags & ImGuiTableFlags_ScrollX) ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None;
        BeginChildEx(name, instance_id, outer_rect.GetSize(), false, child_flags);
        table->InnerWindow = g.CurrentWindow;
        table->WorkRect = table->InnerWindow->WorkRect;
        table->OuterRect = table->InnerWindow->Rect();
        table->InnerRect = table->InnerWindow->InnerRect;
        IM_ASSERT(table->InnerWindow->WindowPadding.x == 0.0f && table->InnerWindow->WindowPadding.y == 0.0f && table->InnerWindow->WindowBorderSize == 0.0f);

        if (table->InnerWindow->SkipItems && outer_window_is_measuring_size)
            table->InnerWindow->SkipItems = false;

        if (instance_no == 0)
        {
            table->HasScrollbarYPrev = table->HasScrollbarYCurr;
            table->HasScrollbarYCurr = false;
        }
        table->HasScrollbarYCurr |= table->InnerWindow->ScrollbarY;
    }
    else
    {
        table->WorkRect = table->OuterRect = table->InnerRect = outer_rect;
    }

    PushOverrideID(id);
    if (instance_no > 0)
        PushOverrideID(instance_id);

    ImGuiWindow* inner_window = table->InnerWindow;
    table->HostIndentX = inner_window->DC.Indent.x;
    table->HostClipRect = inner_window->ClipRect;
    table->HostSkipItems = inner_window->SkipItems;
    temp_data->HostBackupWorkRect = inner_window->WorkRect;
    temp_data->HostBackupParentWorkRect = inner_window->ParentWorkRect;
    temp_data->HostBackupColumnsOffset = outer_window->DC.ColumnsOffset;
    temp_data->HostBackupPrevLineSize = inner_window->DC.PrevLineSize;
    temp_data->HostBackupCurrLineSize = inner_window->DC.CurrLineSize;
    temp_data->HostBackupCursorMaxPos = inner_window->DC.CursorMaxPos;
    temp_data->HostBackupItemWidth = outer_window->DC.ItemWidth;
    temp_data->HostBackupItemWidthStackSize = outer_window->DC.ItemWidthStack.Size;
    inner_window->DC.PrevLineSize = inner_window->DC.CurrLineSize = ImVec2(0.0f, 0.0f);

    if (inner_window != outer_window)
    {
        if (flags & ImGuiTableFlags_BordersOuterV)
            table->HostClipRect.Min.x = ImMin(table->HostClipRect.Min.x + table_border_size, table->HostClipRect.Max.x);
        if (flags & ImGuiTableFlags_BordersOuterH)
            table->HostClipRect.Min.y = ImMin(table->HostClipRect.Min.y + table_border_size, table->HostClipRect.Max.y);
    }

    const bool pad_outer_x = (flags & ImGuiTableFlags_NoPadOuterX) ? false : (flags & ImGuiTableFlags_PadOuterX) ? true : (flags & ImGuiTableFlags_BordersOuterV) != 0;
    const bool pad_inner_x = (flags & ImGuiTableFlags_NoPadInnerX) ? false : true;
    const float inner_spacing_for_border = (flags & ImGuiTableFlags_BordersInnerV) ? table_border_size : 0.0f;
    const float inner_spacing_explicit = (pad_inner_x && (flags & ImGuiTableFlags_BordersInnerV) == 0) ? g.Style.CellPadding.x : 0.0f;
    const float inner_padding_explicit = (pad_inner_x && (flags & ImGuiTableFlags_BordersInnerV) != 0) ? g.Style.CellPadding.x : 0.0f;
    table->CellSpacingX1 = inner_spacing_explicit + inner_spacing_for_border;
    table->CellSpacingX2 = inner_spacing_explicit;
    table->CellPaddingX = inner_padding_explicit;

    const float outer_padding_for_border = (flags & ImGuiTableFlags_BordersOuterV) ? table_border_size : 0.0f;
    const float outer_padding_explicit = pad_outer_x ? g.Style.CellPadding.x : 0.0f;
    table->OuterPaddingX = (outer_padding_for_border + outer_padding_explicit) - table->CellPaddingX;

    table->CurrentColumn = -1;
    table->CurrentRow = -1;
    table->RowBgColorCounter = 0;
    table->LastRowFlags = ImGuiTableRowFlags_None;
    table->InnerClipRect = (inner_window == outer_window) ? table->WorkRect : inner_window->ClipRect;
    table->InnerClipRect.ClipWith(table->WorkRect);    
    table->InnerClipRect.ClipWithFull(table->HostClipRect);
    table->InnerClipRect.Max.y = (flags & ImGuiTableFlags_NoHostExtendY) ? ImMin(table->InnerClipRect.Max.y, inner_window->WorkRect.Max.y) : inner_window->ClipRect.Max.y;

    table->RowPosY1 = table->RowPosY2 = table->WorkRect.Min.y;
    table->RowTextBaseline = 0.0f;
    table->RowCellPaddingY = 0.0f;
    table->FreezeRowsRequest = table->FreezeRowsCount = 0; 
    table->FreezeColumnsRequest = table->FreezeColumnsCount = 0;
    table->IsUnfrozenRows = true;
    table->DeclColumnsCount = table->AngledHeadersCount = 0;
    if (previous_frame_active + 1 < g.FrameCount)
        table->IsActiveIdInTable = false;
    table->AngledHeadersHeight = 0.0f;
    temp_data->AngledHeadersExtraWidth = 0.0f;

    table->BorderColorStrong = GetColorU32(ImGuiCol_TableBorderStrong);
    table->BorderColorLight = GetColorU32(ImGuiCol_TableBorderLight);

    g.CurrentTable = table;
    outer_window->DC.NavIsScrollPushableX = false; 
    outer_window->DC.CurrentTableIdx = table_idx;
    if (inner_window != outer_window) 
        inner_window->DC.CurrentTableIdx = table_idx;

    if ((previous_flags & ImGuiTableFlags_Reorderable) && (flags & ImGuiTableFlags_Reorderable) == 0)
        table->IsResetDisplayOrderRequest = true;

    if (table_idx >= g.TablesLastTimeActive.Size)
        g.TablesLastTimeActive.resize(table_idx + 1, -1.0f);
    g.TablesLastTimeActive[table_idx] = (float)g.Time;
    temp_data->LastTimeActive = (float)g.Time;
    table->MemoryCompacted = false;

    ImGuiTableColumn* old_columns_to_preserve = NULL;
    void* old_columns_raw_data = NULL;
    const int old_columns_count = table->Columns.size();
    if (old_columns_count != 0 && old_columns_count != columns_count)
    {
        old_columns_to_preserve = table->Columns.Data;
        old_columns_raw_data = table->RawData;
        table->RawData = NULL;
    }
    if (table->RawData == NULL)
    {
        TableBeginInitMemory(table, columns_count);
        table->IsInitializing = table->IsSettingsRequestLoad = true;
    }
    if (table->IsResetAllRequest)
        TableResetSettings(table);
    if (table->IsInitializing)
    {
        table->SettingsOffset = -1;
        table->IsSortSpecsDirty = true;
        table->InstanceInteracted = -1;
        table->ContextPopupColumn = -1;
        table->ReorderColumn = table->ResizedColumn = table->LastResizedColumn = -1;
        table->AutoFitSingleColumn = -1;
        table->HoveredColumnBody = table->HoveredColumnBorder = -1;
        for (int n = 0; n < columns_count; n++)
        {
            ImGuiTableColumn* column = &table->Columns[n];
            if (old_columns_to_preserve && n < old_columns_count)
            {
                *column = old_columns_to_preserve[n];
            }
            else
            {
                float width_auto = column->WidthAuto;
                *column = ImGuiTableColumn();
                column->WidthAuto = width_auto;
                column->IsPreserveWidthAuto = true;
                column->IsEnabled = column->IsUserEnabled = column->IsUserEnabledNextFrame = true;
            }
            column->DisplayOrder = table->DisplayOrderToIndex[n] = (ImGuiTableColumnIdx)n;
        }
    }
    if (old_columns_raw_data)
        IM_FREE(old_columns_raw_data);

    if (table->IsSettingsRequestLoad)
        TableLoadSettings(table);

    const float new_ref_scale_unit = g.FontSize;
    if (table->RefScale != 0.0f && table->RefScale != new_ref_scale_unit)
    {
        const float scale_factor = new_ref_scale_unit / table->RefScale;
        for (int n = 0; n < columns_count; n++)
            table->Columns[n].WidthRequest = table->Columns[n].WidthRequest * scale_factor;
    }
    table->RefScale = new_ref_scale_unit;

    inner_window->SkipItems = true;

    if (table->ColumnsNames.Buf.Size > 0)
        table->ColumnsNames.Buf.resize(0);

    TableBeginApplyRequests(table);

    return true;
}

bool c_gui::begin_table(const char* str_id, int columns_count, ImGuiTableFlags flags, const ImVec2& outer_size, float inner_width)
{
    ImGuiID id = GetID(str_id);
    return begin_table_ex(str_id, id, columns_count, flags, outer_size, inner_width);
}

void c_gui::end_table()
{
    ImGuiContext& g = *GImGui;
    ImGuiTable* table = g.CurrentTable;
    IM_ASSERT(table != NULL && "Only call EndTable() if BeginTable() returns true!");

    if (!table->IsLayoutLocked)
        TableUpdateLayout(table);

    const ImGuiTableFlags flags = table->Flags;
    ImGuiWindow* inner_window = table->InnerWindow;
    ImGuiWindow* outer_window = table->OuterWindow;
    ImGuiTableTempData* temp_data = table->TempData;
    IM_ASSERT(inner_window == g.CurrentWindow);
    IM_ASSERT(outer_window == inner_window || outer_window == inner_window->ParentWindow);

    if (table->IsInsideRow)
        TableEndRow(table);

    if (flags & ImGuiTableFlags_ContextMenuInBody)
        if (table->HoveredColumnBody != -1 && !IsAnyItemHovered() && IsMouseReleased(ImGuiMouseButton_Right))
            TableOpenContextMenu((int)table->HoveredColumnBody);

    ImGuiTableInstanceData* table_instance = TableGetInstanceData(table, table->InstanceCurrent);
    inner_window->DC.PrevLineSize = temp_data->HostBackupPrevLineSize;
    inner_window->DC.CurrLineSize = temp_data->HostBackupCurrLineSize;
    inner_window->DC.CursorMaxPos = temp_data->HostBackupCursorMaxPos;
    const float inner_content_max_y = table->RowPosY2;
    IM_ASSERT(table->RowPosY2 == inner_window->DC.CursorPos.y);
    if (inner_window != outer_window)
        inner_window->DC.CursorMaxPos.y = inner_content_max_y;
    else if (!(flags & ImGuiTableFlags_NoHostExtendY))
        table->OuterRect.Max.y = table->InnerRect.Max.y = ImMax(table->OuterRect.Max.y, inner_content_max_y); // Patch OuterRect/InnerRect height
    table->WorkRect.Max.y = ImMax(table->WorkRect.Max.y, table->OuterRect.Max.y);
    table_instance->LastOuterHeight = table->OuterRect.GetHeight();

    if (table->Flags & ImGuiTableFlags_ScrollX)
    {
        const float outer_padding_for_border = (table->Flags & ImGuiTableFlags_BordersOuterV) ? table_border_size : 0.0f;
        float max_pos_x = table->InnerWindow->DC.CursorMaxPos.x;
        if (table->RightMostEnabledColumn != -1)
            max_pos_x = ImMax(max_pos_x, table->Columns[table->RightMostEnabledColumn].WorkMaxX + table->CellPaddingX + table->OuterPaddingX - outer_padding_for_border);
        if (table->ResizedColumn != -1)
            max_pos_x = ImMax(max_pos_x, table->ResizeLockMinContentsX2);
        table->InnerWindow->DC.CursorMaxPos.x = max_pos_x + table->TempData->AngledHeadersExtraWidth;
    }

    if (!(flags & ImGuiTableFlags_NoClip))
        inner_window->DrawList->PopClipRect();
    inner_window->ClipRect = inner_window->DrawList->_ClipRectStack.back();

    if ((flags & ImGuiTableFlags_Borders) != 0)
        TableDrawBorders(table);

#if 0
    if (table->DummyDrawChannel != (ImGuiTableColumnIdx)-1)
    {
        ImDrawChannel* dummy_channel = &table->DrawSplitter._Channels[table->DummyDrawChannel];
        dummy_channel->_CmdBuffer.resize(0);
        dummy_channel->_IdxBuffer.resize(0);
    }
#endif

    ImDrawListSplitter* splitter = table->DrawSplitter;
    splitter->SetCurrentChannel(inner_window->DrawList, 0);
    if ((table->Flags & ImGuiTableFlags_NoClip) == 0)
        TableMergeDrawChannels(table);
    splitter->Merge(inner_window->DrawList);

    float auto_fit_width_for_fixed = 0.0f;
    float auto_fit_width_for_stretched = 0.0f;
    float auto_fit_width_for_stretched_min = 0.0f;
    for (int column_n = 0; column_n < table->ColumnsCount; column_n++)
        if (IM_BITARRAY_TESTBIT(table->EnabledMaskByIndex, column_n))
        {
            ImGuiTableColumn* column = &table->Columns[column_n];
            float column_width_request = ((column->Flags & ImGuiTableColumnFlags_WidthFixed) && !(column->Flags & ImGuiTableColumnFlags_NoResize)) ? column->WidthRequest : TableGetColumnWidthAuto(table, column);
            if (column->Flags & ImGuiTableColumnFlags_WidthFixed)
                auto_fit_width_for_fixed += column_width_request;
            else
                auto_fit_width_for_stretched += column_width_request;
            if ((column->Flags & ImGuiTableColumnFlags_WidthStretch) && (column->Flags & ImGuiTableColumnFlags_NoResize) != 0)
                auto_fit_width_for_stretched_min = ImMax(auto_fit_width_for_stretched_min, column_width_request / (column->StretchWeight / table->ColumnsStretchSumWeights));
        }
    const float width_spacings = (table->OuterPaddingX * 2.0f) + (table->CellSpacingX1 + table->CellSpacingX2) * (table->ColumnsEnabledCount - 1);
    table->ColumnsAutoFitWidth = width_spacings + (table->CellPaddingX * 2.0f) * table->ColumnsEnabledCount + auto_fit_width_for_fixed + ImMax(auto_fit_width_for_stretched, auto_fit_width_for_stretched_min);

    if ((table->Flags & ImGuiTableFlags_ScrollX) == 0 && inner_window != outer_window)
    {
        inner_window->Scroll.x = 0.0f;
    }
    else if (table->LastResizedColumn != -1 && table->ResizedColumn == -1 && inner_window->ScrollbarX && table->InstanceInteracted == table->InstanceCurrent)
    {
        const float neighbor_width_to_keep_visible = table->MinColumnWidth + table->CellPaddingX * 2.0f;
        ImGuiTableColumn* column = &table->Columns[table->LastResizedColumn];
        if (column->MaxX < table->InnerClipRect.Min.x)
            SetScrollFromPosX(inner_window, column->MaxX - inner_window->Pos.x - neighbor_width_to_keep_visible, 1.0f);
        else if (column->MaxX > table->InnerClipRect.Max.x)
            SetScrollFromPosX(inner_window, column->MaxX - inner_window->Pos.x + neighbor_width_to_keep_visible, 1.0f);
    }

    if (table->ResizedColumn != -1 && table->InstanceCurrent == table->InstanceInteracted)
    {
        ImGuiTableColumn* column = &table->Columns[table->ResizedColumn];
        const float new_x2 = (g.IO.MousePos.x - g.ActiveIdClickOffset.x + ImTrunc(table_resize_separator_half_thickness * g.CurrentDpiScale));
        const float new_width = ImTrunc(new_x2 - column->MinX - table->CellSpacingX1 - table->CellPaddingX * 2.0f);
        table->ResizedColumnNextWidth = new_width;
    }

    table->IsActiveIdInTable = (g.ActiveIdIsAlive != 0 && table->IsActiveIdAliveBeforeTable == false);

    IM_ASSERT_USER_ERROR(inner_window->IDStack.back() == table_instance->TableInstanceID, "Mismatching PushID/PopID!");
    IM_ASSERT_USER_ERROR(outer_window->DC.ItemWidthStack.Size >= temp_data->HostBackupItemWidthStackSize, "Too many PopItemWidth!");
    if (table->InstanceCurrent > 0)
        PopID();
    PopID();

    const ImVec2 backup_outer_max_pos = outer_window->DC.CursorMaxPos;
    inner_window->WorkRect = temp_data->HostBackupWorkRect;
    inner_window->ParentWorkRect = temp_data->HostBackupParentWorkRect;
    inner_window->SkipItems = table->HostSkipItems;
    outer_window->DC.CursorPos = table->OuterRect.Min;
    outer_window->DC.ItemWidth = temp_data->HostBackupItemWidth;
    outer_window->DC.ItemWidthStack.Size = temp_data->HostBackupItemWidthStackSize;
    outer_window->DC.ColumnsOffset = temp_data->HostBackupColumnsOffset;

    if (inner_window != outer_window)
    {
        short backup_nav_layers_active_mask = inner_window->DC.NavLayersActiveMask;
        inner_window->DC.NavLayersActiveMask |= 1 << ImGuiNavLayer_Main;
        EndChild();
        inner_window->DC.NavLayersActiveMask = backup_nav_layers_active_mask;
    }
    else
    {
        ItemSize(table->OuterRect.GetSize());
        ItemAdd(table->OuterRect, 0);
    }

    if (table->Flags & ImGuiTableFlags_NoHostExtendX)
    {
        IM_ASSERT((table->Flags & ImGuiTableFlags_ScrollX) == 0);
        outer_window->DC.CursorMaxPos.x = ImMax(backup_outer_max_pos.x, table->OuterRect.Min.x + table->ColumnsAutoFitWidth);
    }
    else if (temp_data->UserOuterSize.x <= 0.0f)
    {
        const float decoration_size = table->TempData->AngledHeadersExtraWidth + ((table->Flags & ImGuiTableFlags_ScrollX) ? inner_window->ScrollbarSizes.x : 0.0f);
        outer_window->DC.IdealMaxPos.x = ImMax(outer_window->DC.IdealMaxPos.x, table->OuterRect.Min.x + table->ColumnsAutoFitWidth + decoration_size - temp_data->UserOuterSize.x);
        outer_window->DC.CursorMaxPos.x = ImMax(backup_outer_max_pos.x, ImMin(table->OuterRect.Max.x, table->OuterRect.Min.x + table->ColumnsAutoFitWidth));
    }
    else
    {
        outer_window->DC.CursorMaxPos.x = ImMax(backup_outer_max_pos.x, table->OuterRect.Max.x);
    }
    if (temp_data->UserOuterSize.y <= 0.0f)
    {
        const float decoration_size = (table->Flags & ImGuiTableFlags_ScrollY) ? inner_window->ScrollbarSizes.y : 0.0f;
        outer_window->DC.IdealMaxPos.y = ImMax(outer_window->DC.IdealMaxPos.y, inner_content_max_y + decoration_size - temp_data->UserOuterSize.y);
        outer_window->DC.CursorMaxPos.y = ImMax(backup_outer_max_pos.y, ImMin(table->OuterRect.Max.y, inner_content_max_y));
    }
    else
    {
        outer_window->DC.CursorMaxPos.y = ImMax(backup_outer_max_pos.y, table->OuterRect.Max.y);
    }

    if (table->IsSettingsDirty)
        TableSaveSettings(table);
    table->IsInitializing = false;

    IM_ASSERT(g.CurrentWindow == outer_window && g.CurrentTable == table);
    IM_ASSERT(g.TablesTempDataStacked > 0);
    temp_data = (--g.TablesTempDataStacked > 0) ? &g.TablesTempData[g.TablesTempDataStacked - 1] : NULL;
    g.CurrentTable = temp_data ? g.Tables.GetByIndex(temp_data->TableIndex) : NULL;
    if (g.CurrentTable)
    {
        g.CurrentTable->TempData = temp_data;
        g.CurrentTable->DrawSplitter = &temp_data->DrawSplitter;
    }
    outer_window->DC.CurrentTableIdx = g.CurrentTable ? g.Tables.GetIndex(g.CurrentTable) : -1;
    NavUpdateCurrentWindowIsScrollPushableX();
}

void c_gui::table_next_row(ImGuiTableRowFlags row_flags, float row_min_height)
{
    ImGuiContext& g = *GImGui;
    ImGuiTable* table = g.CurrentTable;

    if (!table->IsLayoutLocked)
        TableUpdateLayout(table);
    if (table->IsInsideRow)
        TableEndRow(table);

    table->LastRowFlags = table->RowFlags;
    table->RowFlags = row_flags;
    table->RowCellPaddingY = g.Style.CellPadding.y;
    table->RowMinHeight = row_min_height;
    TableBeginRow(table);

    table->RowPosY2 += table->RowCellPaddingY * 2.0f;
    table->RowPosY2 = ImMax(table->RowPosY2, table->RowPosY1 + row_min_height);

    table->InnerWindow->SkipItems = true;
}

bool c_gui::table_set_column_index(int column_n)
{
    ImGuiContext& g = *GImGui;
    ImGuiTable* table = g.CurrentTable;
    if (!table)
        return false;

    if (table->CurrentColumn != column_n)
    {
        if (table->CurrentColumn != -1)
            TableEndCell(table);
        IM_ASSERT(column_n >= 0 && table->ColumnsCount);
        TableBeginCell(table, column_n);
    }

    return table->Columns[column_n].IsRequestOutput;
}