/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "aitoolexecutor.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSet>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QScrollArea>
#include <QPushButton>
#include <QApplication>
#include <cstring>
#include <cmath>
#include <algorithm>

// ── Constructor ────────────────────────────────────────────────────────────────

AIToolExecutor::AIToolExecutor(QObject *parent) : QObject(parent) {}

// ── Target ROM helpers ────────────────────────────────────────────────────────

QByteArray &AIToolExecutor::targetData()
{
    if (m_targetLinkedRomIndex >= 0 &&
        static_cast<qsizetype>(m_targetLinkedRomIndex) < m_project->linkedRoms.size())
        return m_project->linkedRoms[m_targetLinkedRomIndex].data;
    return m_project->currentData;
}

const QByteArray &AIToolExecutor::targetDataConst() const
{
    if (m_targetLinkedRomIndex >= 0 &&
        static_cast<qsizetype>(m_targetLinkedRomIndex) < m_project->linkedRoms.size())
        return m_project->linkedRoms[m_targetLinkedRomIndex].data;
    return m_project->currentData;
}

uint32_t AIToolExecutor::mapOffsetInTarget(const MapInfo &m) const
{
    if (m_targetLinkedRomIndex >= 0 &&
        static_cast<qsizetype>(m_targetLinkedRomIndex) < m_project->linkedRoms.size()) {
        const LinkedRom &lr = m_project->linkedRoms[m_targetLinkedRomIndex];
        if (!lr.mapOffsets.contains(m.name)) return m.address + m.mapDataOffset;  // fallback to main-project address
        if (lr.mapOffsets.contains(m.name))
            return lr.mapOffsets[m.name] + m.mapDataOffset;
    }
    return m.address + m.mapDataOffset;
}

QString AIToolExecutor::noProjectError() const
{
    QJsonObject err; err["error"] = "No project loaded";
    return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
}

static QString mapId(const MapInfo &map)
{
    return QString("0x%1").arg(map.rawAddress, 8, 16, QChar('0'));
}

static void addMapIdentity(QJsonObject &object, const MapInfo &map)
{
    // `mapId` and `address` intentionally have the same value.  The former is
    // a stable identifier; the latter is the explicit argument name accepted
    // by the tools, which is easier for providers to use in function calls.
    object["mapId"] = mapId(map);
    object["address"] = mapId(map);
    object["fileOffset"] = QString("0x%1").arg(map.address + map.mapDataOffset, 8, 16, QChar('0'));
}

void AIToolExecutor::ensureVersionSnapshot(const QString &reason)
{
    if (!m_project) return;
    m_project->snapshotVersion(QString("AI: %1").arg(reason));
}

// ── Tool definitions (static) ─────────────────────────────────────────────────

QVector<AIToolDef> AIToolExecutor::toolDefinitions()
{
    QVector<AIToolDef> defs;

    // list_maps
    {
        AIToolDef d;
        d.name        = "list_maps";
        d.description = "List every project map with name, mapId/address, type, dimensions, and units. "
                        "Use mapId/address to distinguish maps that share a name.";
        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // list_folders
    {
        AIToolDef d;
        d.name        = "list_folders";
        d.description = "List the imported map-folder hierarchy with exact paths and direct map counts.";
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_folder_maps
    {
        AIToolDef d;
        d.name        = "get_folder_maps";
        d.description = "List every map directly assigned to an imported folder.";
        QJsonObject folder;
        folder["type"]        = "string";
        folder["description"] = "Exact folder path returned by list_folders.";
        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = QJsonObject{{"folder", folder}};
        schema["required"] = QJsonArray{"folder"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_project_info
    {
        AIToolDef d;
        d.name        = "get_project_info";
        d.description = "Get project metadata: ECU type, vehicle, ROM size, map count.";
        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_map_values
    {
        AIToolDef d;
        d.name        = "get_map_values";
        d.description = "Read a map's current values as a 2D array of physical values.";
        QJsonObject props;
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name, or '@' followed by a mapId/address returned by list_maps "
                                  "to select one of several same-named maps (for example '@0x8035a8a6').";
        props["name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req;
        req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_original_values
    {
        AIToolDef d;
        d.name        = "get_original_values";
        d.description = "Read a map's original (stock) values before any edits.";
        QJsonObject props;
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name, or '@' followed by a mapId/address returned by list_maps "
                                  "to select one of several same-named maps (for example '@0x8035a8a6').";
        props["name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req;
        req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_modified_maps
    {
        AIToolDef d;
        d.name        = "get_modified_maps";
        d.description = "List maps that differ from the original ROM.";
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // set_map_values
    {
        AIToolDef d;
        d.name        = "set_map_values";
        d.description = "Write a 2D array of physical values to an entire map.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name as returned by list_maps.";
        QJsonObject valuesProp;
        valuesProp["type"]        = "array";
        valuesProp["description"] = "2D array of physical values: outer array = rows, inner array = columns.";
        QJsonObject innerItems;
        innerItems["type"] = "number";
        QJsonObject innerArr;
        innerArr["type"]  = "array";
        innerArr["items"] = innerItems;
        valuesProp["items"] = innerArr;
        QJsonObject props;
        props["name"]   = nameProp;
        props["values"] = valuesProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req;
        req.append("name");
        req.append("values");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // set_cell_value
    {
        AIToolDef d;
        d.name        = "set_cell_value";
        d.description = "Write a single cell value in a map by row/col index.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name, or '@' followed by a mapId/address returned by list_maps.";
        QJsonObject rowProp;
        rowProp["type"]        = "integer";
        rowProp["description"] = "Zero-based row index.";
        QJsonObject colProp;
        colProp["type"]        = "integer";
        colProp["description"] = "Zero-based column index.";
        QJsonObject valProp;
        valProp["type"]        = "number";
        valProp["description"] = "New physical value to write.";
        QJsonObject props;
        props["name"]  = nameProp;
        props["row"]   = rowProp;
        props["col"]   = colProp;
        props["value"] = valProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req;
        req.append("name");
        req.append("row");
        req.append("col");
        req.append("value");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // search_maps
    {
        AIToolDef d;
        d.name        = "search_maps";
        d.description = "Search maps by name pattern (wildcards * ?) or description keyword.";
        QJsonObject queryProp;
        queryProp["type"]        = "string";
        queryProp["description"] = "Search query: a map name pattern (with optional * ? wildcards) or keyword to match against map names and descriptions.";
        QJsonObject props;
        props["query"] = queryProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("query");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_map_info
    {
        AIToolDef d;
        d.name        = "get_map_info";
        d.description = "Get detailed metadata for a map: axes, scaling, address, dimensions.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name, or '@' followed by a mapId/address returned by list_maps.";
        QJsonObject props; props["name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // compare_map_values
    {
        AIToolDef d;
        d.name        = "compare_map_values";
        d.description = "Compare a map's current values against the stock ROM, returning changed cells.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name, or '@' followed by a mapId/address returned by list_maps.";
        QJsonObject props; props["name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // zero_map
    {
        AIToolDef d;
        d.name        = "zero_map";
        d.description = "Set all values in a map to zero.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name to zero out.";
        QJsonObject props; props["name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // scale_map_values
    {
        AIToolDef d;
        d.name        = "scale_map_values";
        d.description = "Multiply all values in a map by a scaling factor.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name, or '@' followed by a mapId/address returned by list_maps.";
        QJsonObject factorProp;
        factorProp["type"]        = "number";
        factorProp["description"] = "Scaling factor to multiply all values by.";
        QJsonObject props;
        props["name"]   = nameProp;
        props["factor"] = factorProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name"); req.append("factor");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // restore_map
    {
        AIToolDef d;
        d.name        = "restore_map";
        d.description = "Restore a map to its original stock values.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name to restore.";
        QJsonObject props; props["name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_map_statistics
    {
        AIToolDef d;
        d.name        = "get_map_statistics";
        d.description = "Get min, max, average, and std deviation of a map's values.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name, or '@' followed by a mapId/address returned by list_maps.";
        QJsonObject props; props["name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // list_groups
    {
        AIToolDef d;
        d.name        = "list_groups";
        d.description = "List all A2L map groups (categories) in the project.";
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_group_maps
    {
        AIToolDef d;
        d.name        = "get_group_maps";
        d.description = "Get all maps belonging to a specific A2L group.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact group name.";
        QJsonObject props; props["name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_axis_values
    {
        AIToolDef d;
        d.name        = "get_axis_values";
        d.description = "Read a map's X and/or Y axis breakpoints and units.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name, or '@' followed by a mapId/address returned by list_maps.";
        QJsonObject props; props["name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_rom_bytes
    {
        AIToolDef d;
        d.name        = "get_rom_bytes";
        d.description = "Read raw hex bytes from the ROM at a given offset.";
        QJsonObject offProp;
        offProp["type"]        = "integer";
        offProp["description"] = "Byte offset (file offset, not ECU address) to start reading.";
        QJsonObject lenProp;
        lenProp["type"]        = "integer";
        lenProp["description"] = "Number of bytes to read (max 512).";
        QJsonObject props;
        props["offset"] = offProp;
        props["length"] = lenProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("offset"); req.append("length");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // fill_map
    {
        AIToolDef d;
        d.name        = "fill_map";
        d.description = "Fill all cells of a map with a single constant value.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject valProp;
        valProp["type"]        = "number";
        valProp["description"] = "The physical value to fill every cell with.";
        QJsonObject props;
        props["name"]  = nameProp;
        props["value"] = valProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name"); req.append("value");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // offset_map_values
    {
        AIToolDef d;
        d.name        = "offset_map_values";
        d.description = "Add a constant offset to all values in a map.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject offProp;
        offProp["type"]        = "number";
        offProp["description"] = "Value to add to every cell (negative to subtract).";
        QJsonObject props;
        props["name"]   = nameProp;
        props["offset"] = offProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name"); req.append("offset");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // clamp_map_values
    {
        AIToolDef d;
        d.name        = "clamp_map_values";
        d.description = "Clamp all values in a map to a min/max range.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject minProp;
        minProp["type"]        = "number";
        minProp["description"] = "Minimum allowed physical value.";
        QJsonObject maxProp;
        maxProp["type"]        = "number";
        maxProp["description"] = "Maximum allowed physical value.";
        QJsonObject props;
        props["name"] = nameProp;
        props["min"]  = minProp;
        props["max"]  = maxProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name"); req.append("min"); req.append("max");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // copy_map_values
    {
        AIToolDef d;
        d.name        = "copy_map_values";
        d.description = "Copy all values from one map to another (same dimensions required).";
        QJsonObject srcProp;
        srcProp["type"]        = "string";
        srcProp["description"] = "Source map name (values to copy FROM).";
        QJsonObject dstProp;
        dstProp["type"]        = "string";
        dstProp["description"] = "Destination map name (values to copy TO).";
        QJsonObject props;
        props["source"]      = srcProp;
        props["destination"] = dstProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("source"); req.append("destination");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // write_rom_bytes
    {
        AIToolDef d;
        d.name        = "write_rom_bytes";
        d.description = "Write raw hex bytes to the ROM at a given offset (max 64 bytes).";
        QJsonObject offProp;
        offProp["type"]        = "integer";
        offProp["description"] = "Byte offset (file offset) to write at.";
        QJsonObject hexProp;
        hexProp["type"]        = "string";
        hexProp["description"] = "Hex string of bytes to write, e.g. 'FF00A5B3'.";
        QJsonObject props;
        props["offset"] = offProp;
        props["hex"]    = hexProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("offset"); req.append("hex");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // smooth_map
    {
        AIToolDef d;
        d.name        = "smooth_map";
        d.description = "Apply neighbor-averaging smoothing to a map.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject iterProp;
        iterProp["type"]        = "integer";
        iterProp["description"] = "Number of smoothing passes (1-10, default 1).";
        QJsonObject props;
        props["name"]       = nameProp;
        props["iterations"] = iterProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // set_axis_values
    {
        AIToolDef d;
        d.name        = "set_axis_values";
        d.description = "Write new breakpoint values to a map's X or Y axis.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject axisProp;
        axisProp["type"]        = "string";
        axisProp["description"] = "Which axis: 'x' or 'y'.";
        QJsonObject valsProp;
        valsProp["type"]        = "array";
        valsProp["description"] = "Array of new physical breakpoint values.";
        QJsonObject numItem; numItem["type"] = "number";
        valsProp["items"] = numItem;
        QJsonObject props;
        props["name"]   = nameProp;
        props["axis"]   = axisProp;
        props["values"] = valsProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name"); req.append("axis"); req.append("values");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // find_maps_by_value
    {
        AIToolDef d;
        d.name        = "find_maps_by_value";
        d.description = "Find maps containing a cell value within a given range.";
        QJsonObject minProp;
        minProp["type"]        = "number";
        minProp["description"] = "Minimum physical value to search for.";
        QJsonObject maxProp;
        maxProp["type"]        = "number";
        maxProp["description"] = "Maximum physical value to search for.";
        QJsonObject props;
        props["min"] = minProp;
        props["max"] = maxProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("min"); req.append("max");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_all_changes_summary
    {
        AIToolDef d;
        d.name        = "get_all_changes_summary";
        d.description = "Summarize all modifications across all maps vs. the original ROM.";
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // ── Linked ROM tools ──────────────────────────────────────────────────────

    // list_linked_roms
    {
        AIToolDef d;
        d.name        = "list_linked_roms";
        d.description = "List all linked ROMs attached to this project.";
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // select_target_rom
    {
        AIToolDef d;
        d.name        = "select_target_rom";
        d.description = "Select which ROM to target for read/write (-1=main, 0+=linked).";
        QJsonObject idxProp;
        idxProp["type"]        = "integer";
        idxProp["description"] = "Target ROM index: -1 for main project ROM, 0+ for linked ROM.";
        QJsonObject props; props["index"] = idxProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("index");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_linked_rom_map_values
    {
        AIToolDef d;
        d.name        = "get_linked_rom_map_values";
        d.description = "Read a map's values from a specific linked ROM by index.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject idxProp;
        idxProp["type"]        = "integer";
        idxProp["description"] = "Linked ROM index (0-based).";
        QJsonObject props;
        props["name"]  = nameProp;
        props["index"] = idxProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name"); req.append("index");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // compare_with_linked_rom
    {
        AIToolDef d;
        d.name        = "compare_with_linked_rom";
        d.description = "Compare a map between the current ROM and a linked ROM.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject idxProp;
        idxProp["type"]        = "integer";
        idxProp["description"] = "Linked ROM index to compare against.";
        QJsonObject props;
        props["name"]  = nameProp;
        props["index"] = idxProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name"); req.append("index");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // ── Version tools ─────────────────────────────────────────────────────────

    // list_versions
    {
        AIToolDef d;
        d.name        = "list_versions";
        d.description = "List all saved version snapshots of the project ROM.";
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_version_map_values
    {
        AIToolDef d;
        d.name        = "get_version_map_values";
        d.description = "Read a map's values from a specific version snapshot by index.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject idxProp;
        idxProp["type"]        = "integer";
        idxProp["description"] = "Version index (0-based, oldest first).";
        QJsonObject props;
        props["name"]  = nameProp;
        props["index"] = idxProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("name"); req.append("index");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // restore_version
    {
        AIToolDef d;
        d.name        = "restore_version";
        d.description = "Restore the entire ROM to a previous version snapshot.";
        QJsonObject idxProp;
        idxProp["type"]        = "integer";
        idxProp["description"] = "Version index to restore (0-based).";
        QJsonObject props; props["index"] = idxProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("index");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // undo_last_change
    {
        AIToolDef d;
        d.name        = "undo_last_change";
        d.description = "Undo the last modification by restoring the most recent snapshot.";
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // batch_zero_maps
    {
        AIToolDef d;
        d.name        = "batch_zero_maps";
        d.description = "Zero out multiple maps matching a name pattern (wildcards supported).";
        QJsonObject patProp;
        patProp["type"]        = "string";
        patProp["description"] = "Name pattern with wildcards (* and ?), e.g. 'DKAT*' or 'AGR*'.";
        QJsonObject props; props["pattern"] = patProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("pattern");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // compare_two_maps
    {
        AIToolDef d;
        d.name        = "compare_two_maps";
        d.description = "Compare values between two maps in the same ROM.";
        QJsonObject map1Prop;
        map1Prop["type"]        = "string";
        map1Prop["description"] = "First map name.";
        QJsonObject map2Prop;
        map2Prop["type"]        = "string";
        map2Prop["description"] = "Second map name.";
        QJsonObject props;
        props["map1"] = map1Prop;
        props["map2"] = map2Prop;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req; req.append("map1"); req.append("map2");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // snapshot_version
    {
        AIToolDef d;
        d.name        = "snapshot_version";
        d.description = "Save a named version snapshot of the current ROM state.";
        QJsonObject labelProp;
        labelProp["type"]        = "string";
        labelProp["description"] = "A short descriptive label for this version snapshot.";
        QJsonObject props;
        props["label"] = labelProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        QJsonArray req;
        req.append("label");
        schema["required"] = req;
        d.inputSchema = schema;
        defs.append(d);
    }

    // batch_modify_maps
    {
        AIToolDef d;
        d.name = "batch_modify_maps";
        d.description = "Apply multiple map modifications at once. Supports zero, fill, scale, offset, restore actions.";
        QJsonObject mapNameProp;
        mapNameProp["type"] = "string";
        mapNameProp["description"] = "Map name";
        QJsonObject actionProp;
        actionProp["type"] = "string";
        actionProp["enum"] = QJsonArray{"zero", "fill", "scale", "offset", "restore"};
        QJsonObject valueProp;
        valueProp["type"] = "number";
        valueProp["description"] = "Value for fill/scale/offset";
        QJsonObject itemProps;
        itemProps["map_name"] = mapNameProp;
        QJsonObject mapAddressProp;
        mapAddressProp["type"] = "string";
        mapAddressProp["description"] = "Stable map address from list_maps/search_maps. Required when map_name is duplicated.";
        itemProps["map_address"] = mapAddressProp;
        itemProps["action"] = actionProp;
        itemProps["value"] = valueProp;
        QJsonObject itemSchema;
        itemSchema["type"] = "object";
        itemSchema["properties"] = itemProps;
        itemSchema["required"] = QJsonArray{"map_name", "action"};
        QJsonObject operationsProp;
        operationsProp["type"] = "array";
        operationsProp["items"] = itemSchema;
        QJsonObject reasonProp;
        reasonProp["type"] = "string";
        reasonProp["description"] = "Reason for the batch modification";
        QJsonObject props;
        props["operations"] = operationsProp;
        props["reason"] = reasonProp;
        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = props;
        schema["required"] = QJsonArray{"operations", "reason"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // ── Analysis tools ────────────────────────────────────────────────────────

    // describe_map_shape
    {
        AIToolDef d;
        d.name        = "describe_map_shape";
        d.description = "Analyze a map's topology: is it monotonic, has a peak, valley, plateau, or cliff? "
                        "Returns min, max, gradient statistics, and a plain-English shape description.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject props; props["map_name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"map_name"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_related_maps
    {
        AIToolDef d;
        d.name        = "get_related_maps";
        d.description = "Find maps related to a given map: same A2L group, shared axis signals, "
                        "or semantically related names. Useful for understanding dependencies before making changes.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject props; props["map_name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"map_name"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // identify_map_purpose
    {
        AIToolDef d;
        d.name        = "identify_map_purpose";
        d.description = "Identify what an ECU map does based on its name pattern, value range, units, "
                        "and description. Returns a best-guess purpose with a confidence level (low/medium/high).";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject props; props["map_name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"map_name"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // validate_map_changes
    {
        AIToolDef d;
        d.name        = "validate_map_changes";
        d.description = "Check whether proposed values for a map are within safe physical bounds. "
                        "Reports cells that exceed typical engineering limits for the map's unit type.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject valuesProp;
        valuesProp["type"]        = "array";
        valuesProp["description"] = "2D array of proposed physical values (rows × cols).";
        valuesProp["items"]       = QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "number"}}}};
        QJsonObject props;
        props["map_name"]       = nameProp;
        props["proposed_values"] = valuesProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"map_name", "proposed_values"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // detect_anomalies
    {
        AIToolDef d;
        d.name        = "detect_anomalies";
        d.description = "Scan one map (or all modified maps) for suspicious patterns: flat maps that "
                        "should have gradients, extreme outlier cells, or values at raw integer limits.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "Map name to scan. If empty, scans all modified maps.";
        QJsonObject props; props["map_name"] = nameProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // summarize_all_differences
    {
        AIToolDef d;
        d.name        = "summarize_all_differences";
        d.description = "Get a compact, structured summary of every map that differs from the original ROM: "
                        "map name, cells changed, min/max delta. Use before apply_delta_to_rom or for change reports.";
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = QJsonObject();
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // get_tuning_notes
    {
        AIToolDef d;
        d.name        = "get_tuning_notes";
        d.description = "Read the tuning logbook. Pass a map name to filter entries for that map, "
                        "or leave empty to get the full session log including dyno runs.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "Optional map name filter. Leave empty for all entries.";
        QJsonObject limitProp;
        limitProp["type"]        = "integer";
        limitProp["description"] = "Maximum entries to return (default 20).";
        QJsonObject props;
        props["map_name"] = nameProp;
        props["limit"]    = limitProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray();
        d.inputSchema = schema;
        defs.append(d);
    }

    // confidence_search
    {
        AIToolDef d;
        d.name        = "confidence_search";
        d.description = "Search maps by name or description with relevance scores. "
                        "Returns matches ranked by confidence (0-100) based on name similarity, "
                        "description keywords, and value-range matching.";
        QJsonObject queryProp;
        queryProp["type"]        = "string";
        queryProp["description"] = "Search query — map name fragment, unit, or functional keyword.";
        QJsonObject props; props["query"] = queryProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"query"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // evaluate_map_expression
    {
        AIToolDef d;
        d.name        = "evaluate_map_expression";
        d.description = "Apply a mathematical expression to every cell in a map. "
                        "Use 'v' for current physical value and 'r','c' for row/col index. "
                        "Examples: 'v * 1.1' (scale 10%%), 'v + 2.5' (offset), 'v * (1 + r*0.01)' (row-varying scale). "
                        "Shows a preview before applying.";
        QJsonObject nameProp;
        nameProp["type"]        = "string";
        nameProp["description"] = "The exact map name.";
        QJsonObject exprProp;
        exprProp["type"]        = "string";
        exprProp["description"] = "Math expression using v (current value), r (row), c (col). Standard operators: + - * / pow() min() max() abs().";
        QJsonObject props;
        props["map_name"]   = nameProp;
        props["expression"] = exprProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"map_name", "expression"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // apply_delta_to_rom
    {
        AIToolDef d;
        d.name        = "apply_delta_to_rom";
        d.description = "Copy all map modifications from a linked ROM into the current ROM. "
                        "Only maps that differ from original in the source are copied. "
                        "Shows a preview of which maps will change before applying.";
        QJsonObject srcProp;
        srcProp["type"]        = "integer";
        srcProp["description"] = "Source linked ROM index (0-based). Use list_linked_roms to find indices.";
        QJsonObject mapsProp;
        mapsProp["type"]        = "array";
        mapsProp["description"] = "Legacy optional list of map names. Every name must be unique; use map_refs for duplicate names.";
        mapsProp["items"]       = QJsonObject{{"type", "string"}};
        QJsonObject mapRefProps;
        mapRefProps["name"] = QJsonObject{{"type", "string"}};
        mapRefProps["address"] = QJsonObject{{"type", "string"},
                                                {"description", "Stable address from list_maps/search_maps."}};
        QJsonObject mapRefItem{{"type", "object"}, {"properties", mapRefProps}};
        QJsonObject mapRefsProp;
        mapRefsProp["type"] = "array";
        mapRefsProp["description"] = "Optional exact map references to copy. Use address for every duplicate name.";
        mapRefsProp["items"] = mapRefItem;
        QJsonObject props;
        props["source_index"] = srcProp;
        props["map_names"]    = mapsProp;
        props["map_refs"]     = mapRefsProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"source_index"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // append_tuning_note
    {
        AIToolDef d;
        d.name        = "append_tuning_note";
        d.description = "Add an entry to the tuning logbook. Use this to record what was changed "
                        "and why, so the tuning history is preserved for future reference.";
        QJsonObject mapProp;
        mapProp["type"]        = "string";
        mapProp["description"] = "Optional map name this note relates to.";
        QJsonObject msgProp;
        msgProp["type"]        = "string";
        msgProp["description"] = "The note text.";
        QJsonObject catProp;
        catProp["type"]        = "string";
        catProp["description"] = "Category: modification, note, recipe, anomaly, or observation.";
        catProp["enum"]        = QJsonArray{"modification", "note", "recipe", "anomaly", "observation"};
        QJsonObject props;
        props["map_name"] = mapProp;
        props["message"]  = msgProp;
        props["category"] = catProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"message"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // log_dyno_result
    {
        AIToolDef d;
        d.name        = "log_dyno_result";
        d.description = "Record a dyno run result in the project logbook. After logging, "
                        "the AI can compare results across runs to track tuning progress.";
        QJsonObject powerProp;
        powerProp["type"]        = "number";
        powerProp["description"] = "Peak power reading.";
        QJsonObject unitProp;
        unitProp["type"]        = "string";
        unitProp["description"] = "Power unit: PS or kW.";
        unitProp["enum"]        = QJsonArray{"PS", "kW"};
        QJsonObject torqueProp;
        torqueProp["type"]        = "number";
        torqueProp["description"] = "Peak torque in Nm.";
        QJsonObject rpmProp;
        rpmProp["type"]        = "integer";
        rpmProp["description"] = "RPM at peak power.";
        QJsonObject notesProp;
        notesProp["type"]        = "string";
        notesProp["description"] = "Tuner notes for this run (conditions, observations).";
        QJsonObject props;
        props["peak_power"]  = powerProp;
        props["power_unit"]  = unitProp;
        props["peak_torque"] = torqueProp;
        props["rpm_at_power"]= rpmProp;
        props["notes"]       = notesProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"peak_power", "power_unit"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // undo_with_reason
    {
        AIToolDef d;
        d.name        = "undo_with_reason";
        d.description = "Restore a previous ROM version and log an explanation of why "
                        "the change was reverted. Safer than undo_last_change as it records the reasoning.";
        QJsonObject idxProp;
        idxProp["type"]        = "integer";
        idxProp["description"] = "Version index to restore (0-based). Use list_versions to find indices.";
        QJsonObject reasonProp;
        reasonProp["type"]        = "string";
        reasonProp["description"] = "Explanation for why this rollback is being performed.";
        QJsonObject props;
        props["version_index"] = idxProp;
        props["reason"]        = reasonProp;
        QJsonObject schema;
        schema["type"]       = "object";
        schema["properties"] = props;
        schema["required"]   = QJsonArray{"version_index", "reason"};
        d.inputSchema = schema;
        defs.append(d);
    }

    // Every map-targeting tool accepts an optional address alongside its name.
    // Addresses are returned by list/search tools and are required whenever a
    // name is duplicated.  Keeping `name` preserves compatibility with older
    // providers and callers for maps whose names are unique.
    const QSet<QString> mapTargetFields = {
        "name", "map_name", "source", "destination", "map1", "map2"
    };
    const QSet<QString> mapTargetTools = {
        "get_map_values", "get_original_values", "set_map_values", "set_cell_value",
        "get_map_info", "compare_map_values", "zero_map", "scale_map_values",
        "restore_map", "get_map_statistics", "get_axis_values", "fill_map",
        "offset_map_values", "clamp_map_values", "copy_map_values", "smooth_map",
        "set_axis_values", "get_linked_rom_map_values", "compare_with_linked_rom",
        "get_version_map_values", "compare_two_maps", "describe_map_shape",
        "get_related_maps", "identify_map_purpose", "validate_map_changes",
        "detect_anomalies", "evaluate_map_expression"
    };
    for (AIToolDef &d : defs) {
        if (!mapTargetTools.contains(d.name)) continue;
        QJsonObject schema = d.inputSchema;
        QJsonObject props = schema.value("properties").toObject();
        bool targetsMap = false;
        for (const QString &field : mapTargetFields) {
            if (!props.contains(field)) continue;
            const QString addressField = field + "_address";
            QJsonObject addressProp;
            addressProp["type"] = "string";
            addressProp["description"] = "Stable map address returned by list_maps or search_maps. "
                                         "Supply this whenever more than one map has the same name.";
            props[addressField] = addressProp;
            targetsMap = true;
        }
        if (targetsMap) {
            d.description += " Map names may be duplicated: always pass the matching *_address returned by a map listing/search when selecting a duplicate.";
            schema["properties"] = props;
            d.inputSchema = schema;
        }
    }

    return defs;
}

// ── execute() dispatcher ───────────────────────────────────────────────────────

QString AIToolExecutor::execute(const QString &toolName, QJsonObject input)
{
    // Translate an address companion field into an internal selector before
    // dispatching.  This keeps the individual tools backwards-compatible while
    // ensuring all of them resolve the exact duplicate selected by the model.
    const auto selectAddress = [&input](const QString &nameField) {
        const QString address = input.value(nameField + "_address").toString().trimmed();
        if (!address.isEmpty()) input[nameField] = "@" + address;
    };
    for (const QString &field : {QStringLiteral("name"), QStringLiteral("map_name"),
                                 QStringLiteral("source"), QStringLiteral("destination"),
                                 QStringLiteral("map1"), QStringLiteral("map2")}) {
        selectAddress(field);
    }

    // Read tools
    if (toolName == "list_maps")          return toolListMaps();
    if (toolName == "list_folders")       return toolListFolders();
    if (toolName == "get_folder_maps")    return toolGetFolderMaps(input);
    if (toolName == "get_project_info")   return toolGetProjectInfo();
    if (toolName == "get_map_values")     return toolGetMapValues(input);
    if (toolName == "get_original_values")return toolGetOriginalValues(input);
    if (toolName == "get_modified_maps")  return toolGetModifiedMaps();
    if (toolName == "search_maps")        return toolSearchMaps(input);
    if (toolName == "get_map_info")       return toolGetMapInfo(input);
    if (toolName == "compare_map_values") return toolCompareMapValues(input);
    if (toolName == "get_map_statistics") return toolGetMapStatistics(input);
    if (toolName == "list_groups")        return toolListGroups();
    if (toolName == "get_group_maps")     return toolGetGroupMaps(input);
    if (toolName == "get_axis_values")    return toolGetAxisValues(input);
    if (toolName == "get_rom_bytes")      return toolGetRomBytes(input);
    if (toolName == "find_maps_by_value") return toolFindMapsByValue(input);
    if (toolName == "get_all_changes_summary") return toolGetAllChangesSummary();

    // Linked ROM tools
    if (toolName == "list_linked_roms")         return toolListLinkedRoms();
    if (toolName == "select_target_rom")        return toolSelectTargetRom(input);
    if (toolName == "get_linked_rom_map_values")return toolGetLinkedRomMapValues(input);
    if (toolName == "compare_with_linked_rom")  return toolCompareWithLinkedRom(input);

    // Version tools
    if (toolName == "list_versions")         return toolListVersions();
    if (toolName == "get_version_map_values") return toolGetVersionMapValues(input);
    if (toolName == "restore_version")       return toolRestoreVersion(input);

    // Write tools
    if (toolName == "set_map_values")     return toolSetMapValues(input);
    if (toolName == "set_cell_value")     return toolSetCellValue(input);
    if (toolName == "zero_map")           return toolZeroMap(input);
    if (toolName == "scale_map_values")   return toolScaleMapValues(input);
    if (toolName == "restore_map")        return toolRestoreMap(input);
    if (toolName == "fill_map")           return toolFillMap(input);
    if (toolName == "offset_map_values")  return toolOffsetMapValues(input);
    if (toolName == "clamp_map_values")   return toolClampMapValues(input);
    if (toolName == "copy_map_values")    return toolCopyMapValues(input);
    if (toolName == "write_rom_bytes")    return toolWriteRomBytes(input);
    if (toolName == "smooth_map")         return toolSmoothMap(input);
    if (toolName == "set_axis_values")    return toolSetAxisValues(input);
    if (toolName == "undo_last_change")   return toolUndoLastChange();
    if (toolName == "batch_zero_maps")    return toolBatchZeroMaps(input);
    if (toolName == "batch_modify_maps")  return toolBatchModifyMaps(input);
    if (toolName == "compare_two_maps")   return toolCompareTwoMaps(input);
    if (toolName == "snapshot_version")   return toolSnapshotVersion(input);

    // Analysis tools
    if (toolName == "describe_map_shape")     return toolDescribeMapShape(input);
    if (toolName == "get_related_maps")       return toolGetRelatedMaps(input);
    if (toolName == "identify_map_purpose")   return toolIdentifyMapPurpose(input);
    if (toolName == "validate_map_changes")   return toolValidateMapChanges(input);
    if (toolName == "detect_anomalies")       return toolDetectAnomalies(input);
    if (toolName == "summarize_all_differences") return toolSummarizeAllDifferences();
    if (toolName == "get_tuning_notes")       return toolGetTuningNotes(input);
    if (toolName == "confidence_search")      return toolConfidenceSearch(input);

    // New write tools
    if (toolName == "evaluate_map_expression")return toolEvaluateMapExpression(input);
    if (toolName == "apply_delta_to_rom")     return toolApplyDeltaToRom(input);
    if (toolName == "append_tuning_note")     return toolAppendTuningNote(input);
    if (toolName == "log_dyno_result")        return toolLogDynoResult(input);
    if (toolName == "undo_with_reason")       return toolUndoWithReason(input);

    QJsonObject err;
    err["error"] = "Unknown tool: " + toolName;
    return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
}

// ── Helpers ────────────────────────────────────────────────────────────────────

QString AIToolExecutor::mapNotFound(const QString &name) const
{
    QJsonObject obj;
    if (m_project && !name.startsWith('@')) {
        QJsonArray candidates;
        for (const MapInfo &m : m_project->maps) {
            if (m.name != name) continue;
            candidates.append(QJsonObject{
                {"name", m.name},
                {"mapId", QString("0x%1").arg(m.rawAddress, 8, 16, QChar('0'))},
                {"address", QString("0x%1").arg(m.rawAddress, 8, 16, QChar('0'))},
                {"dataSize", m.dataSize},
                {"dimensions", QString("%1x%2").arg(m.dimensions.y).arg(m.dimensions.x)},
                {"unit", m.scaling.unit}
            });
        }
        if (candidates.size() > 1) {
            obj["error"] = "Ambiguous map name: " + name;
            obj["message"] = "Pass the selected candidate's address using the matching *_address field.";
            obj["candidates"] = candidates;
            return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        }
    }
    obj["error"] = "Map not found: " + (name.startsWith('@') ? name.mid(1) : name);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

MapInfo *AIToolExecutor::findMap(const QString &name)
{
    if (!m_project) return nullptr;
    if (name.startsWith('@')) {
        bool ok = false;
        const uint32_t rawAddress = name.mid(1).toUInt(&ok, 0);
        if (!ok) return nullptr;
        MapInfo *match = nullptr;
        for (MapInfo &m : m_project->maps) {
            if (m.rawAddress != rawAddress) continue;
            if (match) return nullptr; // An address must also uniquely identify a map.
            match = &m;
        }
        return match;
    }

    MapInfo *match = nullptr;
    for (MapInfo &m : m_project->maps) {
        if (m.name != name) continue;
        if (match) return nullptr; // Never silently choose the first duplicate.
        match = &m;
    }
    return match;
}

// ── Read tools ─────────────────────────────────────────────────────────────────

QString AIToolExecutor::toolListMaps()
{
    if (!m_project) {
        QJsonObject err;
        err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    QJsonArray arr;
    for (const MapInfo &m : m_project->maps) {
        QJsonObject obj;
        obj["name"]        = m.name;
        obj["description"] = m.description;
        obj["type"]        = m.type;
        obj["cols"]        = m.dimensions.x;
        obj["rows"]        = m.dimensions.y;
        addMapIdentity(obj, m);
        obj["units"]       = m.scaling.unit;
        obj["dataSize"]    = m.dataSize;
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString AIToolExecutor::toolListFolders()
{
    if (!m_project) return noProjectError();

    QStringList paths;
    for (const MapInfo &map : m_project->maps) {
        const QString path = map.folderPath.trimmed();
        if (!path.isEmpty() && !paths.contains(path)) paths.append(path);
    }
    paths.sort(Qt::CaseInsensitive);

    QJsonArray folders;
    for (const QString &path : paths) {
        int mapCount = 0;
        for (const MapInfo &map : m_project->maps)
            if (map.folderPath.compare(path, Qt::CaseInsensitive) == 0) ++mapCount;

        const int slash = path.lastIndexOf('/');
        QJsonObject folder;
        folder["path"] = path;
        folder["name"] = slash >= 0 ? path.mid(slash + 1) : path;
        folder["parent"] = slash >= 0 ? path.left(slash) : QString();
        folder["mapCount"] = mapCount;
        folders.append(folder);
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"count", folders.size()},
                                                          {"folders", folders}})
                                 .toJson(QJsonDocument::Compact));
}

QString AIToolExecutor::toolGetFolderMaps(const QJsonObject &input)
{
    if (!m_project) return noProjectError();
    const QString requested = input.value("folder").toString().trimmed();
    if (requested.isEmpty()) {
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"error", "Folder path is required."}})
                                     .toJson(QJsonDocument::Compact));
    }

    QStringList matchingPaths;
    for (const MapInfo &map : m_project->maps) {
        const QString path = map.folderPath.trimmed();
        if (path.isEmpty() || matchingPaths.contains(path)) continue;
        if (path.compare(requested, Qt::CaseInsensitive) == 0
            || path.section('/', -1).compare(requested, Qt::CaseInsensitive) == 0)
            matchingPaths.append(path);
    }
    if (matchingPaths.size() != 1) {
        QJsonObject result;
        result["error"] = matchingPaths.isEmpty() ? "Folder not found." : "Folder name is ambiguous.";
        result["requested"] = requested;
        result["candidates"] = QJsonArray::fromStringList(matchingPaths);
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    const QString path = matchingPaths.first();
    QJsonArray maps;
    for (const MapInfo &map : m_project->maps) {
        if (map.folderPath.compare(path, Qt::CaseInsensitive) != 0) continue;
        QJsonObject entry;
        entry["name"] = map.name;
        entry["description"] = map.description;
        entry["type"] = map.type;
        entry["cols"] = map.dimensions.x;
        entry["rows"] = map.dimensions.y;
        entry["unit"] = map.scaling.unit;
        addMapIdentity(entry, map);
        maps.append(entry);
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"folder", path},
                                                          {"count", maps.size()},
                                                          {"maps", maps}})
                                 .toJson(QJsonDocument::Compact));
}

QString AIToolExecutor::toolGetProjectInfo()
{
    if (!m_project) {
        QJsonObject err;
        err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Count modified maps inline
    int modifiedCount = 0;
    if (!m_project->originalData.isEmpty()) {
        for (const MapInfo &m : m_project->maps) {
            if (m.address + m.length > targetDataConst().size()) continue;
            if (m.address + m.length > m_project->originalData.size()) continue;
            if (std::memcmp(targetDataConst().constData() +m.address,
                            m_project->originalData.constData() + m.address,
                            m.length) != 0) {
                ++modifiedCount;
            }
        }
    }

    QJsonObject obj;
    obj["name"]           = m_project->name;
    obj["ecuType"]        = m_project->ecuType;
    obj["brand"]          = m_project->brand;
    obj["model"]          = m_project->model;
    obj["year"]           = m_project->year;
    obj["romSize"]        = targetDataConst().size();
    obj["baseAddress"]    = QString("0x%1").arg(m_project->baseAddress, 8, 16, QChar('0'));
    obj["byteOrder"]      = (m_project->byteOrder == ByteOrder::BigEndian) ? "big-endian" : "little-endian";
    obj["mapCount"]       = m_project->maps.size();
    obj["linkedRomCount"] = m_project->linkedRoms.size();
    obj["modifiedMaps"]   = modifiedCount;
    obj["versionCount"]   = m_project->versions.size();
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// Shared helper for reading map values from any ROM data with a custom base offset
static QString readMapValues(const MapInfo &m, const QByteArray &romData, ByteOrder byteOrder,
                             uint32_t baseOff)
{
    const auto *data = reinterpret_cast<const uint8_t *>(romData.constData());
    int dataLen = romData.size();

    int cols = qMin(m.dimensions.x, 64);
    int rows = qMin(m.dimensions.y, 64);

    QJsonArray rowsArr;
    for (int r = 0; r < rows; ++r) {
        QJsonArray rowArr;
        for (int c = 0; c < cols; ++c) {
            int cellIdx;
            if (m.columnMajor)
                cellIdx = c * m.dimensions.y + r;
            else
                cellIdx = r * m.dimensions.x + c;
            uint32_t offset = baseOff + (uint32_t)(cellIdx * m.dataSize);
            uint32_t raw = readRomValue(data, dataLen, offset, m.dataSize, byteOrder);
            double phys = m.scaling.toPhysical(signExtendRaw(raw, m.dataSize, m.dataSigned));
            // Round to 6 significant figures to keep JSON compact
            rowArr.append(QJsonValue(phys));
        }
        rowsArr.append(rowArr);
    }

    QJsonObject result;
    result["name"]     = m.name;
    addMapIdentity(result, m);
    result["cols"]     = cols;
    result["rows"]     = rows;
    result["unit"]     = m.scaling.unit;
    result["values"]   = rowsArr;
    if (cols < m.dimensions.x || rows < m.dimensions.y) {
        result["truncated"] = true;
        result["full_cols"] = m.dimensions.x;
        result["full_rows"] = m.dimensions.y;
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString AIToolExecutor::toolGetMapValues(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString name = input["name"].toString();
    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);
    return readMapValues(*m, targetDataConst(), m_project->byteOrder, mapOffsetInTarget(*m));
}

QString AIToolExecutor::toolGetOriginalValues(const QJsonObject &input)
{
    if (!m_project) return noProjectError();
    QString name = input["name"].toString();
    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);
    if (m_project->originalData.isEmpty()) {
        QJsonObject err; err["error"] = "No original ROM snapshot available";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    return readMapValues(*m, m_project->originalData, m_project->byteOrder,
                         m->address + m->mapDataOffset);
}

QString AIToolExecutor::toolGetModifiedMaps()
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    if (m_project->originalData.isEmpty()) {
        QJsonObject err; err["error"] = "No original ROM snapshot available";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    QJsonArray arr;
    QJsonArray refs;
    for (const MapInfo &m : m_project->maps) {
        if (m.address + m.length > targetDataConst().size()) continue;
        if (m.address + m.length > m_project->originalData.size()) continue;
        if (std::memcmp(targetDataConst().constData() +m.address,
                        m_project->originalData.constData() + m.address,
                        m.length) != 0) {
            arr.append(m.name); // Legacy field retained for existing clients.
            QJsonObject ref{{"name", m.name}};
            addMapIdentity(ref, m);
            refs.append(ref);
        }
    }
    QJsonObject result;
    result["modifiedMaps"] = arr;
    result["modifiedMapRefs"] = refs;
    result["count"]        = arr.size();
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ── search_maps ───────────────────────────────────────────────────────────────

QString AIToolExecutor::toolSearchMaps(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString query = input["query"].toString().trimmed();
    if (query.isEmpty()) {
        QJsonObject err; err["error"] = "Empty search query";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Build a QRegularExpression from wildcard pattern or use as substring
    bool hasWildcard = query.contains('*') || query.contains('?');
    QJsonArray results;

    for (const MapInfo &m : m_project->maps) {
        bool match = false;
        if (hasWildcard) {
            // Convert wildcard to regex
            QString pattern = QRegularExpression::escape(query);
            pattern.replace("\\*", ".*").replace("\\?", ".");
            QRegularExpression rx("^" + pattern + "$", QRegularExpression::CaseInsensitiveOption);
            match = rx.match(m.name).hasMatch();
        } else {
            // Substring match in name or description
            match = m.name.contains(query, Qt::CaseInsensitive)
                 || m.description.contains(query, Qt::CaseInsensitive);
        }

        if (match) {
            QJsonObject obj;
            obj["name"]        = m.name;
            obj["description"] = m.description;
            obj["type"]        = m.type;
            obj["cols"]        = m.dimensions.x;
            obj["rows"]        = m.dimensions.y;
            obj["units"]       = m.scaling.unit;
            obj["dataSize"]    = m.dataSize;
            addMapIdentity(obj, m);
            results.append(obj);
        }
    }

    QJsonObject res;
    res["query"]   = query;
    res["count"]   = results.size();
    res["results"] = results;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── get_map_info ──────────────────────────────────────────────────────────────

QString AIToolExecutor::toolGetMapInfo(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString name = input["name"].toString();
    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    QJsonObject obj;
    obj["name"]        = m->name;
    obj["description"] = m->description;
    obj["type"]        = m->type;
    obj["cols"]        = m->dimensions.x;
    obj["rows"]        = m->dimensions.y;
    obj["dataSize"]    = m->dataSize;
    obj["columnMajor"] = m->columnMajor;
    obj["rawAddress"]  = QString("0x%1").arg(m->rawAddress, 8, 16, QChar('0'));
    addMapIdentity(obj, *m);
    obj["length"]      = m->length;

    // Scaling info
    QJsonObject scaling;
    scaling["unit"] = m->scaling.unit;
    scaling["format"] = m->scaling.format;
    if (m->scaling.type == CompuMethod::Type::Linear) {
        scaling["type"]   = "linear";
        scaling["coeffA"] = m->scaling.linA;
        scaling["coeffB"] = m->scaling.linB;
        scaling["formula"] = QString("phys = %1 * raw + %2").arg(m->scaling.linA).arg(m->scaling.linB);
    } else if (m->scaling.type == CompuMethod::Type::RationalFunction) {
        scaling["type"] = "rational";
    } else {
        scaling["type"] = "identical";
    }
    obj["scaling"] = scaling;

    // X axis info
    if (m->dimensions.x > 1) {
        QJsonObject xAxis;
        xAxis["inputName"] = m->xAxis.inputName;
        xAxis["unit"]      = m->xAxis.scaling.unit;
        xAxis["points"]    = m->xAxis.ptsCount;
        if (!m->xAxis.fixedValues.isEmpty()) {
            QJsonArray vals;
            for (double v : m->xAxis.fixedValues) vals.append(v);
            xAxis["values"] = vals;
        }
        obj["xAxis"] = xAxis;
    }

    // Y axis info
    if (m->dimensions.y > 1) {
        QJsonObject yAxis;
        yAxis["inputName"] = m->yAxis.inputName;
        yAxis["unit"]      = m->yAxis.scaling.unit;
        yAxis["points"]    = m->yAxis.ptsCount;
        if (!m->yAxis.fixedValues.isEmpty()) {
            QJsonArray vals;
            for (double v : m->yAxis.fixedValues) vals.append(v);
            yAxis["values"] = vals;
        }
        obj["yAxis"] = yAxis;
    }

    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ── compare_map_values ────────────────────────────────────────────────────────

QString AIToolExecutor::toolCompareMapValues(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    if (m_project->originalData.isEmpty()) {
        QJsonObject err; err["error"] = "No original ROM snapshot available";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString name = input["name"].toString();
    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    const auto *curData  = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    const auto *origData = reinterpret_cast<const uint8_t *>(m_project->originalData.constData());
    int dataLen = targetDataConst().size();
    int origLen = m_project->originalData.size();
    uint32_t baseOff = mapOffsetInTarget(*m);
    int cols = m->dimensions.x;
    int rows = m->dimensions.y;

    QJsonArray diffs;
    int totalChanged = 0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);

            uint32_t curRaw  = readRomValue(curData,  dataLen,  off, m->dataSize, m_project->byteOrder);
            uint32_t origRaw = readRomValue(origData, origLen,  off, m->dataSize, m_project->byteOrder);

            if (curRaw != origRaw) {
                double curPhys  = m->scaling.toPhysical(signExtendRaw(curRaw, m->dataSize, m->dataSigned));
                double origPhys = m->scaling.toPhysical(signExtendRaw(origRaw, m->dataSize, m->dataSigned));
                QJsonObject d;
                d["row"]      = r;
                d["col"]      = c;
                d["original"] = origPhys;
                d["current"]  = curPhys;
                d["delta"]    = curPhys - origPhys;
                diffs.append(d);
                ++totalChanged;
            }
        }
    }

    QJsonObject res;
    res["map"]          = name;
    res["unit"]         = m->scaling.unit;
    res["totalCells"]   = cols * rows;
    res["changedCells"] = totalChanged;
    res["identical"]    = (totalChanged == 0);
    if (totalChanged > 0)
        res["changes"] = diffs;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── get_map_statistics ────────────────────────────────────────────────────────

QString AIToolExecutor::toolGetMapStatistics(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString name = input["name"].toString();
    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    const auto *data = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    uint32_t baseOff = mapOffsetInTarget(*m);
    int cols = m->dimensions.x;
    int rows = m->dimensions.y;
    int total = cols * rows;

    double minVal = 1e30, maxVal = -1e30, sum = 0;
    QVector<double> values;
    values.reserve(total);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            uint32_t raw = readRomValue(data, dataLen, off, m->dataSize, m_project->byteOrder);
            double phys = m->scaling.toPhysical(signExtendRaw(raw, m->dataSize, m->dataSigned));
            values.append(phys);
            sum += phys;
            if (phys < minVal) minVal = phys;
            if (phys > maxVal) maxVal = phys;
        }
    }

    double avg = (total > 0) ? sum / total : 0;
    double variance = 0;
    for (double v : values) variance += (v - avg) * (v - avg);
    double stddev = (total > 1) ? std::sqrt(variance / (total - 1)) : 0;

    QJsonObject res;
    res["map"]    = name;
    res["unit"]   = m->scaling.unit;
    res["cells"]  = total;
    res["min"]    = minVal;
    res["max"]    = maxVal;
    res["avg"]    = avg;
    res["stddev"] = stddev;
    res["range"]  = maxVal - minVal;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── list_groups ───────────────────────────────────────────────────────────────

QString AIToolExecutor::toolListGroups()
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    QJsonArray arr;
    for (const A2LGroup &g : m_project->groups) {
        QJsonObject obj;
        obj["name"]        = g.name;
        obj["description"] = g.description;
        obj["mapCount"]    = (int)g.characteristics.size();
        obj["subGroups"]   = (int)g.subGroups.size();
        arr.append(obj);
    }

    QJsonObject res;
    res["groups"] = arr;
    res["count"]  = arr.size();
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── get_group_maps ────────────────────────────────────────────────────────────

QString AIToolExecutor::toolGetGroupMaps(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString groupName = input["name"].toString();

    // Find the group
    const A2LGroup *found = nullptr;
    for (const A2LGroup &g : m_project->groups) {
        if (g.name.compare(groupName, Qt::CaseInsensitive) == 0) {
            found = &g;
            break;
        }
    }
    if (!found) {
        QJsonObject err; err["error"] = "Group not found: " + groupName;
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    QJsonArray maps;
    for (const QString &charName : found->characteristics) {
        // A2L groups store names, so a duplicate name can legitimately refer
        // to more than one physical map. Return every matching map with its
        // address instead of losing all but the first one.
        for (const MapInfo &candidate : m_project->maps) {
            if (candidate.name != charName) continue;
            const MapInfo *m = &candidate;
            QJsonObject obj;
            obj["name"]        = m->name;
            obj["description"] = m->description;
            obj["type"]        = m->type;
            obj["cols"]        = m->dimensions.x;
            obj["rows"]        = m->dimensions.y;
            obj["units"]       = m->scaling.unit;
            obj["dataSize"]    = m->dataSize;
            addMapIdentity(obj, *m);
            maps.append(obj);
        }
    }

    QJsonObject res;
    res["group"]       = found->name;
    res["description"] = found->description;
    res["maps"]        = maps;
    res["count"]       = maps.size();
    if (!found->subGroups.isEmpty()) {
        QJsonArray subs;
        for (const QString &s : found->subGroups) subs.append(s);
        res["subGroups"] = subs;
    }
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── get_axis_values ───────────────────────────────────────────────────────────

QString AIToolExecutor::toolGetAxisValues(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString name = input["name"].toString();
    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    const auto *data = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();

    auto readAxis = [&](const AxisInfo &axis, int count) -> QJsonObject {
        QJsonObject ax;
        ax["inputName"] = axis.inputName;
        ax["unit"]      = axis.scaling.unit;
        ax["points"]    = count;

        QJsonArray vals;
        if (!axis.fixedValues.isEmpty()) {
            for (int i = 0; i < qMin(count, (int)axis.fixedValues.size()); ++i)
                vals.append(axis.fixedValues[i]);
        } else if (axis.hasPtsAddress) {
            uint32_t addr = axis.ptsAddress;
            for (int i = 0; i < count; ++i) {
                uint32_t off = addr + (uint32_t)(i * axis.ptsDataSize);
                uint32_t raw = readRomValue(data, dataLen, off, axis.ptsDataSize, m_project->byteOrder);
                double phys = axis.scaling.toPhysical(signExtendRaw(raw, axis.ptsDataSize, axis.ptsSigned));
                vals.append(phys);
            }
        }
        if (!vals.isEmpty()) ax["values"] = vals;
        return ax;
    };

    QJsonObject res;
    res["map"] = name;
    if (m->dimensions.x > 1)
        res["xAxis"] = readAxis(m->xAxis, m->dimensions.x);
    if (m->dimensions.y > 1)
        res["yAxis"] = readAxis(m->yAxis, m->dimensions.y);
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── get_rom_bytes ─────────────────────────────────────────────────────────────

QString AIToolExecutor::toolGetRomBytes(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    int offset = input["offset"].toInt(-1);
    int length = qMin(input["length"].toInt(16), 512);  // cap at 512

    if (offset < 0 || offset >= targetDataConst().size()) {
        QJsonObject err; err["error"] = QString("Offset %1 out of range (ROM size: %2)")
                                            .arg(offset).arg(targetDataConst().size());
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    int available = qMin(length, targetDataConst().size() - offset);
    QByteArray bytes = targetDataConst().mid(offset, available);

    // Format as hex with spaces every 16 bytes
    QString hexStr;
    for (int i = 0; i < bytes.size(); ++i) {
        if (i > 0 && i % 16 == 0) hexStr += '\n';
        else if (i > 0) hexStr += ' ';
        hexStr += QString("%1").arg((uint8_t)bytes[i], 2, 16, QChar('0')).toUpper();
    }

    QJsonObject res;
    res["offset"]    = offset;
    res["length"]    = available;
    res["hex"]       = hexStr;
    res["offsetHex"] = QString("0x%1").arg((uint32_t)offset, 8, 16, QChar('0'));
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── find_maps_by_value ────────────────────────────────────────────────────────

QString AIToolExecutor::toolFindMapsByValue(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    double minVal = input["min"].toDouble();
    double maxVal = input["max"].toDouble();

    const auto *data = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();

    QJsonArray results;
    for (const MapInfo &m : m_project->maps) {
        uint32_t baseOff = mapOffsetInTarget(m);
        int cols = m.dimensions.x;
        int rows = m.dimensions.y;
        bool found = false;
        int matchCount = 0;

        for (int r = 0; r < rows && !found; ++r) {
            for (int c = 0; c < cols; ++c) {
                int cellIdx = m.columnMajor ? (c * rows + r) : (r * cols + c);
                uint32_t off = baseOff + (uint32_t)(cellIdx * m.dataSize);
                if ((int)(off + m.dataSize) > dataLen) continue;
                uint32_t raw = readRomValue(data, dataLen, off, m.dataSize, m_project->byteOrder);
                double phys = m.scaling.toPhysical(signExtendRaw(raw, m.dataSize, m.dataSigned));
                if (phys >= minVal && phys <= maxVal) {
                    ++matchCount;
                    found = true;
                }
            }
        }
        if (found) {
            QJsonObject obj;
            obj["name"]        = m.name;
            obj["description"] = m.description;
            obj["type"]        = m.type;
            obj["units"]       = m.scaling.unit;
            obj["dataSize"]    = m.dataSize;
            addMapIdentity(obj, m);
            results.append(obj);
        }
    }

    QJsonObject res;
    res["searchRange"] = QString("[%1, %2]").arg(minVal).arg(maxVal);
    res["count"]       = results.size();
    res["results"]     = results;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── get_all_changes_summary ───────────────────────────────────────────────────

QString AIToolExecutor::toolGetAllChangesSummary()
{
    if (!m_project) return noProjectError();
    if (m_project->originalData.isEmpty()) {
        QJsonObject err; err["error"] = "No original ROM snapshot available";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const auto *curData  = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    const auto *origData = reinterpret_cast<const uint8_t *>(m_project->originalData.constData());
    int curLen  = targetDataConst().size();
    int origLen = m_project->originalData.size();

    QJsonArray changes;
    int totalChanged = 0;

    for (const MapInfo &m : m_project->maps) {
        uint32_t baseOff = mapOffsetInTarget(m);
        uint32_t origBase = m.address + m.mapDataOffset;
        int cols = m.dimensions.x;
        int rows = m.dimensions.y;
        int cellsChanged = 0;
        double minDelta = 1e30, maxDelta = -1e30;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int cellIdx = m.columnMajor ? (c * rows + r) : (r * cols + c);
                uint32_t curOff  = baseOff + (uint32_t)(cellIdx * m.dataSize);
                uint32_t origOff = origBase + (uint32_t)(cellIdx * m.dataSize);
                if ((int)(curOff + m.dataSize) > curLen) continue;
                if ((int)(origOff + m.dataSize) > origLen) continue;

                uint32_t curRaw  = readRomValue(curData,  curLen,  curOff,  m.dataSize, m_project->byteOrder);
                uint32_t origRaw = readRomValue(origData, origLen, origOff, m.dataSize, m_project->byteOrder);
                if (curRaw != origRaw) {
                    double delta = m.scaling.toPhysical(signExtendRaw(curRaw, m.dataSize, m.dataSigned)) - m.scaling.toPhysical(signExtendRaw(origRaw, m.dataSize, m.dataSigned));
                    if (delta < minDelta) minDelta = delta;
                    if (delta > maxDelta) maxDelta = delta;
                    ++cellsChanged;
                }
            }
        }

        if (cellsChanged > 0) {
            QJsonObject obj;
            obj["map"]          = m.name;
            addMapIdentity(obj, m);
            obj["description"]  = m.description;
            obj["unit"]         = m.scaling.unit;
            obj["cellsChanged"] = cellsChanged;
            obj["totalCells"]   = cols * rows;
            obj["minDelta"]     = minDelta;
            obj["maxDelta"]     = maxDelta;
            changes.append(obj);
            ++totalChanged;
        }
    }

    QJsonObject res;
    res["mapsChanged"]   = totalChanged;
    res["totalMaps"]     = m_project->maps.size();
    res["targetRom"]     = (m_targetLinkedRomIndex >= 0) ? "linked ROM #" + QString::number(m_targetLinkedRomIndex) : "main project ROM";
    res["changes"]       = changes;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── Linked ROM tools ──────────────────────────────────────────────────────────

QString AIToolExecutor::toolListLinkedRoms()
{
    if (!m_project) return noProjectError();

    QJsonArray arr;
    for (int i = 0; i < m_project->linkedRoms.size(); ++i) {
        const LinkedRom &lr = m_project->linkedRoms[i];
        QJsonObject obj;
        obj["index"]       = i;
        obj["label"]       = lr.label;
        obj["filePath"]    = lr.filePath;
        obj["size"]        = lr.data.size();
        obj["isReference"] = lr.isReference;
        obj["importedAt"]  = lr.importedAt.toString(Qt::ISODate);
        obj["mapsLinked"]  = lr.mapOffsets.size();
        arr.append(obj);
    }

    QJsonObject res;
    res["linkedRoms"]    = arr;
    res["count"]         = arr.size();
    res["currentTarget"] = m_targetLinkedRomIndex;
    res["targetLabel"]   = (m_targetLinkedRomIndex >= 0 && m_targetLinkedRomIndex < m_project->linkedRoms.size())
                           ? m_project->linkedRoms[m_targetLinkedRomIndex].label
                           : "main project ROM";
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

QString AIToolExecutor::toolSelectTargetRom(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    int idx = input["index"].toInt(-1);
    if (idx >= m_project->linkedRoms.size()) {
        QJsonObject err; err["error"] = QString("Linked ROM index %1 out of range (have %2)")
                                            .arg(idx).arg(m_project->linkedRoms.size());
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    m_targetLinkedRomIndex = idx;

    QJsonObject res;
    res["success"]  = true;
    res["target"]   = idx;
    res["label"]    = (idx >= 0) ? m_project->linkedRoms[idx].label : "main project ROM";
    res["romSize"]  = targetDataConst().size();
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

QString AIToolExecutor::toolGetLinkedRomMapValues(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    QString name = input["name"].toString();
    int idx = input["index"].toInt(-1);

    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    if (idx < 0 || idx >= m_project->linkedRoms.size()) {
        QJsonObject err; err["error"] = QString("Linked ROM index %1 out of range").arg(idx);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const LinkedRom &lr = m_project->linkedRoms[idx];
    if (!lr.mapOffsets.contains(m->name)) {
        QJsonObject err; err["error"] = QString("Map '%1' not linked in ROM '%2'").arg(m->name, lr.label);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    uint32_t baseOff = lr.mapOffsets[m->name] + m->mapDataOffset;
    return readMapValues(*m, lr.data, m_project->byteOrder, baseOff);
}

QString AIToolExecutor::toolCompareWithLinkedRom(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    QString name = input["name"].toString();
    int idx = input["index"].toInt(-1);

    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    if (idx < 0 || idx >= m_project->linkedRoms.size()) {
        QJsonObject err; err["error"] = QString("Linked ROM index %1 out of range").arg(idx);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const LinkedRom &lr = m_project->linkedRoms[idx];
    if (!lr.mapOffsets.contains(m->name)) {
        QJsonObject err; err["error"] = QString("Map '%1' not linked in ROM '%2'").arg(m->name, lr.label);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const auto *curData = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    const auto *lrData  = reinterpret_cast<const uint8_t *>(lr.data.constData());
    int curLen = targetDataConst().size();
    int lrLen  = lr.data.size();

    uint32_t curBase = mapOffsetInTarget(*m);
    uint32_t lrBase  = lr.mapOffsets[m->name] + m->mapDataOffset;
    int cols = m->dimensions.x;
    int rows = m->dimensions.y;

    QJsonArray diffs;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t curOff = curBase + (uint32_t)(cellIdx * m->dataSize);
            uint32_t lrOff  = lrBase  + (uint32_t)(cellIdx * m->dataSize);
            if ((int)(curOff + m->dataSize) > curLen) continue;
            if ((int)(lrOff + m->dataSize) > lrLen) continue;

            uint32_t curRaw = readRomValue(curData, curLen, curOff, m->dataSize, m_project->byteOrder);
            uint32_t lrRaw  = readRomValue(lrData,  lrLen,  lrOff,  m->dataSize, m_project->byteOrder);

            if (curRaw != lrRaw) {
                QJsonObject d;
                d["row"]      = r;
                d["col"]      = c;
                d["current"]  = m->scaling.toPhysical(signExtendRaw(curRaw, m->dataSize, m->dataSigned));
                d["linked"]   = m->scaling.toPhysical(signExtendRaw(lrRaw, m->dataSize, m->dataSigned));
                diffs.append(d);
            }
        }
    }

    QJsonObject res;
    res["map"]            = name;
    res["unit"]           = m->scaling.unit;
    res["linkedRom"]      = lr.label;
    res["changedCells"]   = diffs.size();
    res["totalCells"]     = cols * rows;
    if (!diffs.isEmpty()) res["changes"] = diffs;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── Version tools ─────────────────────────────────────────────────────────────

QString AIToolExecutor::toolListVersions()
{
    if (!m_project) return noProjectError();

    QJsonArray arr;
    for (int i = 0; i < m_project->versions.size(); ++i) {
        const ProjectVersion &v = m_project->versions[i];
        QJsonObject obj;
        obj["index"]   = i;
        obj["name"]    = v.name;
        obj["created"] = v.created.toString(Qt::ISODate);
        obj["size"]    = v.data.size();
        arr.append(obj);
    }

    QJsonObject res;
    res["versions"] = arr;
    res["count"]    = arr.size();
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

QString AIToolExecutor::toolGetVersionMapValues(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    QString name = input["name"].toString();
    int idx = input["index"].toInt(-1);

    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    if (idx < 0 || idx >= m_project->versions.size()) {
        QJsonObject err; err["error"] = QString("Version index %1 out of range (have %2)")
                                            .arg(idx).arg(m_project->versions.size());
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    return readMapValues(*m, m_project->versions[idx].data, m_project->byteOrder,
                         m->address + m->mapDataOffset);
}

QString AIToolExecutor::toolRestoreVersion(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    int idx = input["index"].toInt(-1);
    if (idx < 0 || idx >= m_project->versions.size()) {
        QJsonObject err; err["error"] = QString("Version index %1 out of range").arg(idx);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const ProjectVersion &v = m_project->versions[idx];

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Restore Version"),
        tr("AI wants to restore the ROM to version:\n\n"
                "\"%1\" (created %2)\n\n"
                "This will replace all current data. Continue?")
            .arg(v.name, v.created.toString("yyyy-MM-dd HH:mm:ss")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    // Snapshot current state before restoring
    ensureVersionSnapshot("before restore to: " + v.name);
    m_project->restoreVersion(idx);
    emit projectModified();

    QJsonObject res;
    res["success"]  = true;
    res["restored"] = v.name;
    res["index"]    = idx;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── Write tools ───────────────────────────────────────────────────────────────

QString AIToolExecutor::toolSetMapValues(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    {
        const int dx = m->dimensions.x, dy = m->dimensions.y;
        if (dx <= 0 || dy <= 0 || dx > 100000 || dy > 100000 ||
            qint64(dx) * dy * m->dataSize > qint64(targetDataConst().size())) {
            QJsonObject err; err["error"] = "Map dimensions out of range";
            return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
        }
    }

    QJsonArray newRowsJson = input["values"].toArray();
    if (newRowsJson.isEmpty()) {
        QJsonObject err; err["error"] = "No values provided";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    int rows = qMin((int)newRowsJson.size(), m->dimensions.y);
    int cols = m->dimensions.x;

    // Collect proposed values and read current values for display
    // proposed[r][c]
    QVector<QVector<double>> proposed(rows, QVector<double>(cols, 0.0));
    QVector<QVector<double>> current(rows, QVector<double>(cols, 0.0));

    const auto *data = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    uint32_t baseOff = mapOffsetInTarget(*m);

    for (int r = 0; r < rows; ++r) {
        QJsonArray rowArr = newRowsJson[r].toArray();
        int effCols = qMin((int)rowArr.size(), cols);
        for (int c = 0; c < effCols; ++c) {
            proposed[r][c] = rowArr[c].toDouble();
        }
    }
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * m->dimensions.y + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            uint32_t raw = readRomValue(data, dataLen, off, m->dataSize, m_project->byteOrder);
            current[r][c] = m->scaling.toPhysical(signExtendRaw(raw, m->dataSize, m->dataSigned));
        }
    }

    // Build confirmation dialog
    QDialog dlg(qApp->activeWindow());
    dlg.setWindowTitle(QString("AI wants to modify: %1").arg(mapName));
    dlg.setMinimumSize(600, 400);
    dlg.setStyleSheet("QDialog { background:#0d1117; color:#c9d1d9; }"
                      "QLabel  { color:#c9d1d9; }"
                      "QTableWidget { background:#161b22; color:#c9d1d9; gridline-color:#30363d; }"
                      "QHeaderView::section { background:#21262d; color:#c9d1d9; border:1px solid #30363d; }"
                      "QPushButton { background:#1f6feb; color:#fff; border-radius:4px; padding:4px 12px; }"
                      "QPushButton:hover { background:#388bfd; }"
                      "QPushButton[text='Cancel'] { background:#21262d; }");

    QVBoxLayout *vlay = new QVBoxLayout(&dlg);

    QLabel *info = new QLabel(
        QString("The AI wants to write %1 × %2 values to <b>%3</b>.<br>"
                "Unit: %4 &nbsp;|&nbsp; Proposed changes are highlighted.")
        .arg(rows).arg(cols).arg(mapName.toHtmlEscaped()).arg(m->scaling.unit.toHtmlEscaped()));
    info->setWordWrap(true);
    vlay->addWidget(info);

    // Table: cols = map cols × 2 (current | proposed per column), rows = map rows
    int dispCols = qMin(cols, 32);
    int dispRows = qMin(rows, 32);
    QTableWidget *table = new QTableWidget(dispRows, dispCols * 2, &dlg);
    // Headers
    QStringList hdr;
    for (int c = 0; c < dispCols; ++c) {
        hdr << QString("Col%1\nCur").arg(c);
        hdr << QString("Col%1\nNew").arg(c);
    }
    table->setHorizontalHeaderLabels(hdr);
    table->horizontalHeader()->setDefaultSectionSize(60);
    table->verticalHeader()->setDefaultSectionSize(22);

    for (int r = 0; r < dispRows; ++r) {
        for (int c = 0; c < dispCols; ++c) {
            double curVal  = current[r][c];
            double propVal = proposed[r][c];
            bool changed   = (qAbs(curVal - propVal) > 1e-9);

            auto *curItem  = new QTableWidgetItem(m->scaling.formatValue(curVal));
            auto *propItem = new QTableWidgetItem(m->scaling.formatValue(propVal));
            curItem->setFlags(Qt::ItemIsEnabled);
            propItem->setFlags(Qt::ItemIsEnabled);
            if (changed) {
                curItem->setBackground(QColor("#5d2d2d"));
                propItem->setBackground(QColor("#1a4a1a"));
                propItem->setForeground(QColor("#56d364"));
            }
            table->setItem(r, c * 2,     curItem);
            table->setItem(r, c * 2 + 1, propItem);
        }
    }
    if (rows > dispRows || cols > dispCols) {
        QLabel *truncNote = new QLabel(
            QString("(Showing first %1 rows × %2 cols of %3 × %4)")
            .arg(dispRows).arg(dispCols).arg(rows).arg(cols));
        truncNote->setStyleSheet("color:#8b949e; font-style:italic;");
        vlay->addWidget(truncNote);
    }

    QScrollArea *scroll = new QScrollArea(&dlg);
    scroll->setWidget(table);
    scroll->setWidgetResizable(true);
    vlay->addWidget(scroll);

    QDialogButtonBox *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    btns->button(QDialogButtonBox::Ok)->setText(tr("Approve"));
    vlay->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    // Snapshot before writing
    ensureVersionSnapshot("AI edit: " + mapName);

    // Write values
    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    int writtenCells = 0;
    for (int r = 0; r < rows; ++r) {
        QJsonArray rowArr = newRowsJson[r].toArray();
        int effCols = qMin((int)rowArr.size(), cols);
        for (int c = 0; c < effCols; ++c) {
            double phys = proposed[r][c];
            uint32_t raw;
            if (m->dataSigned) {
                double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
                double maxS = (m->dataSize == 1) ? 127.0 : (m->dataSize == 4) ? 2147483647.0 : 32767.0;
                raw = (uint32_t)(int32_t)qBound(minS, std::round(m->scaling.toRaw(phys)), maxS);
            } else {
                raw = (uint32_t)qRound(m->scaling.toRaw(phys));
            }
            int cellIdx  = m->columnMajor ? (c * m->dimensions.y + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            writeRomValue(rawData, targetDataConst().size(), off, m->dataSize,
                          m_project->byteOrder, raw);
            ++writtenCells;
        }
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]      = true;
    res["map"]          = mapName;
    res["cellsWritten"] = writtenCells;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

QString AIToolExecutor::toolSetCellValue(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    int row   = input["row"].toInt(-1);
    int col   = input["col"].toInt(-1);
    double newPhys = input["value"].toDouble();

    if (row < 0 || row >= m->dimensions.y || col < 0 || col >= m->dimensions.x) {
        QJsonObject err;
        err["error"] = QString("Cell [%1,%2] out of range for map %3 (%4×%5)")
                       .arg(row).arg(col).arg(mapName).arg(m->dimensions.y).arg(m->dimensions.x);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Read current value
    const auto *data = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    uint32_t baseOff = mapOffsetInTarget(*m);
    int cellIdx      = m->columnMajor ? (col * m->dimensions.y + row) : (row * m->dimensions.x + col);
    uint32_t off     = baseOff + (uint32_t)(cellIdx * m->dataSize);
    uint32_t rawVal  = readRomValue(data, dataLen, off, m->dataSize, m_project->byteOrder);
    double curPhys   = m->scaling.toPhysical(signExtendRaw(rawVal, m->dataSize, m->dataSigned));

    QString question = QString(
        "AI wants to modify <b>%1</b> cell [row %2, col %3]:\n\n"
        "Current value: %4 %5\n"
        "New value:     %6 %5\n\n"
        "Apply this change?")
        .arg(mapName)
        .arg(row).arg(col)
        .arg(m->scaling.formatValue(curPhys))
        .arg(m->scaling.unit)
        .arg(m->scaling.formatValue(newPhys));

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Cell Edit"),
        question,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot(QString("AI cell edit: %1[%2,%3]").arg(mapName).arg(row).arg(col));

    uint32_t newRaw;
    if (m->dataSigned) {
        double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
        double maxS = (m->dataSize == 1) ? 127.0 : (m->dataSize == 4) ? 2147483647.0 : 32767.0;
        newRaw = (uint32_t)(int32_t)qBound(minS, std::round(m->scaling.toRaw(newPhys)), maxS);
    } else {
        newRaw = (uint32_t)qRound(m->scaling.toRaw(newPhys));
    }
    writeRomValue(reinterpret_cast<uint8_t *>(targetData().data()),
                  targetDataConst().size(), off, m->dataSize,
                  m_project->byteOrder, newRaw);
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"] = true;
    res["map"]     = mapName;
    res["row"]     = row;
    res["col"]     = col;
    res["oldValue"]= m->scaling.formatValue(curPhys);
    res["newValue"]= m->scaling.formatValue(newPhys);
    res["unit"]    = m->scaling.unit;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── zero_map ──────────────────────────────────────────────────────────────────

QString AIToolExecutor::toolZeroMap(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    {
        const int dx = m->dimensions.x, dy = m->dimensions.y;
        if (dx <= 0 || dy <= 0 || dx > 100000 || dy > 100000 ||
            qint64(dx) * dy * m->dataSize > qint64(targetDataConst().size())) {
            QJsonObject err; err["error"] = "Map dimensions out of range";
            return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
        }
    }

    int cols = m->dimensions.x;
    int rows = m->dimensions.y;

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Zero Map"),
        tr("AI wants to set all %1 × %2 = %3 cells in <b>%4</b> to zero.\n\n"
                "This is commonly used to disable ECU monitoring functions.\n\n"
                "Apply this change?")
            .arg(rows).arg(cols).arg(rows * cols).arg(mapName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot("AI zero: " + mapName);

    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    uint32_t baseOff = mapOffsetInTarget(*m);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            uint32_t rawZero;
            if (m->dataSigned) {
                double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
                double maxS = (m->dataSize == 1) ? 127.0 : (m->dataSize == 4) ? 2147483647.0 : 32767.0;
                rawZero = (uint32_t)(int32_t)qBound(minS, std::round(m->scaling.toRaw(0.0)), maxS);
            } else {
                rawZero = (uint32_t)qRound(m->scaling.toRaw(0.0));
            }
            writeRomValue(rawData, targetDataConst().size(), off, m->dataSize,
                          m_project->byteOrder, rawZero);
        }
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]      = true;
    res["map"]          = mapName;
    res["cellsWritten"] = rows * cols;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── scale_map_values ──────────────────────────────────────────────────────────

QString AIToolExecutor::toolScaleMapValues(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    {
        const int dx = m->dimensions.x, dy = m->dimensions.y;
        if (dx <= 0 || dy <= 0 || dx > 100000 || dy > 100000 ||
            qint64(dx) * dy * m->dataSize > qint64(targetDataConst().size())) {
            QJsonObject err; err["error"] = "Map dimensions out of range";
            return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
        }
    }

    double factor = input["factor"].toDouble(1.0);
    int cols = m->dimensions.x;
    int rows = m->dimensions.y;

    QString pct;
    if (factor > 1.0)      pct = QString("+%1%").arg((factor - 1.0) * 100.0, 0, 'f', 1);
    else if (factor < 1.0) pct = QString("%1%").arg((factor - 1.0) * 100.0, 0, 'f', 1);
    else                   pct = "0% (no change)";

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Scale Map"),
        tr("AI wants to multiply all values in <b>%1</b> by <b>%2</b> (%3).\n\n"
                "This affects %4 cells. Apply this change?")
            .arg(mapName).arg(factor, 0, 'f', 4).arg(pct).arg(rows * cols),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot(QString("AI scale %1 ×%2").arg(mapName).arg(factor));

    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    const auto *constData = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    uint32_t baseOff = mapOffsetInTarget(*m);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            uint32_t curRaw = readRomValue(constData, dataLen, off, m->dataSize, m_project->byteOrder);
            double curPhys  = m->scaling.toPhysical(signExtendRaw(curRaw, m->dataSize, m->dataSigned));
            double newPhys  = curPhys * factor;
            uint32_t newRaw;
            if (m->dataSigned) {
                double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
                double maxS = (m->dataSize == 1) ? 127.0 : (m->dataSize == 4) ? 2147483647.0 : 32767.0;
                newRaw = (uint32_t)(int32_t)qBound(minS, std::round(m->scaling.toRaw(newPhys)), maxS);
            } else {
                newRaw = (uint32_t)qRound(m->scaling.toRaw(newPhys));
            }
            writeRomValue(rawData, dataLen, off, m->dataSize, m_project->byteOrder, newRaw);
        }
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]      = true;
    res["map"]          = mapName;
    res["factor"]       = factor;
    res["cellsWritten"] = rows * cols;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── restore_map ───────────────────────────────────────────────────────────────

QString AIToolExecutor::toolRestoreMap(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    if (m_project->originalData.isEmpty()) {
        QJsonObject err; err["error"] = "No original ROM snapshot available";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Restore Map"),
        tr("AI wants to restore <b>%1</b> to its original stock values.\n\n"
                "This will undo all changes to this map. Apply?")
            .arg(mapName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot("AI restore: " + mapName);

    // Copy the original map bytes over the current ones
    uint32_t mapStart = m->address;
    int mapLen = m->length;
    if ((int)(mapStart + mapLen) <= m_project->originalData.size()
        && (int)(mapStart + mapLen) <= targetDataConst().size()) {
        std::memcpy(targetData().data() +mapStart,
                    m_project->originalData.constData() + mapStart,
                    mapLen);
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]       = true;
    res["map"]           = mapName;
    res["bytesRestored"] = mapLen;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── fill_map ──────────────────────────────────────────────────────────────────

QString AIToolExecutor::toolFillMap(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    {
        const int dx = m->dimensions.x, dy = m->dimensions.y;
        if (dx <= 0 || dy <= 0 || dx > 100000 || dy > 100000 ||
            qint64(dx) * dy * m->dataSize > qint64(targetDataConst().size())) {
            QJsonObject err; err["error"] = "Map dimensions out of range";
            return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
        }
    }

    double fillVal = input["value"].toDouble();
    int cols = m->dimensions.x;
    int rows = m->dimensions.y;

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Fill Map"),
        tr("AI wants to fill all %1 cells in <b>%2</b> with <b>%3 %4</b>.\n\nApply?")
            .arg(rows * cols).arg(mapName).arg(m->scaling.formatValue(fillVal)).arg(m->scaling.unit),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot("AI fill: " + mapName);
    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    uint32_t baseOff = mapOffsetInTarget(*m);
    uint32_t rawVal;
    if (m->dataSigned) {
        double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
        double maxS = (m->dataSize == 1) ? 127.0 : (m->dataSize == 4) ? 2147483647.0 : 32767.0;
        rawVal = (uint32_t)(int32_t)qBound(minS, std::round(m->scaling.toRaw(fillVal)), maxS);
    } else {
        rawVal = (uint32_t)qRound(m->scaling.toRaw(fillVal));
    }

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            writeRomValue(rawData, targetDataConst().size(), off, m->dataSize,
                          m_project->byteOrder, rawVal);
        }
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]      = true;
    res["map"]          = mapName;
    res["fillValue"]    = fillVal;
    res["cellsWritten"] = rows * cols;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── offset_map_values ─────────────────────────────────────────────────────────

QString AIToolExecutor::toolOffsetMapValues(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    {
        const int dx = m->dimensions.x, dy = m->dimensions.y;
        if (dx <= 0 || dy <= 0 || dx > 100000 || dy > 100000 ||
            qint64(dx) * dy * m->dataSize > qint64(targetDataConst().size())) {
            QJsonObject err; err["error"] = "Map dimensions out of range";
            return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
        }
    }

    double offset = input["offset"].toDouble();
    int cols = m->dimensions.x;
    int rows = m->dimensions.y;

    QString desc = (offset >= 0)
        ? QString("+%1 %2").arg(offset).arg(m->scaling.unit)
        : QString("%1 %2").arg(offset).arg(m->scaling.unit);

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Offset Map"),
        tr("AI wants to add <b>%1</b> to all %2 cells in <b>%3</b>.\n\nApply?")
            .arg(desc).arg(rows * cols).arg(mapName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot(QString("AI offset %1 %2").arg(mapName).arg(desc));
    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    const auto *constData = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    uint32_t baseOff = mapOffsetInTarget(*m);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            uint32_t curRaw = readRomValue(constData, dataLen, off, m->dataSize, m_project->byteOrder);
            double curPhys  = m->scaling.toPhysical(signExtendRaw(curRaw, m->dataSize, m->dataSigned));
            double newPhys  = curPhys + offset;
            uint32_t newRaw;
            if (m->dataSigned) {
                double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
                double maxS = (m->dataSize == 1) ? 127.0 : (m->dataSize == 4) ? 2147483647.0 : 32767.0;
                newRaw = (uint32_t)(int32_t)qBound(minS, std::round(m->scaling.toRaw(newPhys)), maxS);
            } else {
                newRaw = (uint32_t)qRound(m->scaling.toRaw(newPhys));
            }
            writeRomValue(rawData, dataLen, off, m->dataSize, m_project->byteOrder, newRaw);
        }
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]      = true;
    res["map"]          = mapName;
    res["offset"]       = offset;
    res["cellsWritten"] = rows * cols;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── clamp_map_values ──────────────────────────────────────────────────────────

QString AIToolExecutor::toolClampMapValues(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    double minVal = input["min"].toDouble();
    double maxVal = input["max"].toDouble();
    int cols = m->dimensions.x;
    int rows = m->dimensions.y;

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Clamp Map"),
        tr("AI wants to clamp all values in <b>%1</b> to range [%2, %3] %4.\n\nApply?")
            .arg(mapName).arg(minVal).arg(maxVal).arg(m->scaling.unit),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot(QString("AI clamp %1 [%2,%3]").arg(mapName).arg(minVal).arg(maxVal));
    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    const auto *constData = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    uint32_t baseOff = mapOffsetInTarget(*m);
    int clamped = 0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            uint32_t curRaw = readRomValue(constData, dataLen, off, m->dataSize, m_project->byteOrder);
            double curPhys  = m->scaling.toPhysical(signExtendRaw(curRaw, m->dataSize, m->dataSigned));
            double newPhys  = qBound(minVal, curPhys, maxVal);
            if (qAbs(newPhys - curPhys) > 1e-9) {
                uint32_t newRaw;
                if (m->dataSigned) {
                    double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
                    double maxS = (m->dataSize == 1) ? 127.0 : (m->dataSize == 4) ? 2147483647.0 : 32767.0;
                    newRaw = (uint32_t)(int32_t)qBound(minS, std::round(m->scaling.toRaw(newPhys)), maxS);
                } else {
                    newRaw = (uint32_t)qRound(m->scaling.toRaw(newPhys));
                }
                writeRomValue(rawData, dataLen, off, m->dataSize, m_project->byteOrder, newRaw);
                ++clamped;
            }
        }
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]      = true;
    res["map"]          = mapName;
    res["min"]          = minVal;
    res["max"]          = maxVal;
    res["cellsClamped"] = clamped;
    res["totalCells"]   = rows * cols;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── copy_map_values ───────────────────────────────────────────────────────────

QString AIToolExecutor::toolCopyMapValues(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString srcName = input["source"].toString();
    QString dstName = input["destination"].toString();
    MapInfo *src = findMap(srcName);
    MapInfo *dst = findMap(dstName);
    if (!src) return mapNotFound(srcName);
    if (!dst) return mapNotFound(dstName);

    if (src->dimensions.x != dst->dimensions.x || src->dimensions.y != dst->dimensions.y) {
        QJsonObject err;
        err["error"] = QString("Dimension mismatch: %1 is %2×%3 but %4 is %5×%6")
            .arg(srcName).arg(src->dimensions.y).arg(src->dimensions.x)
            .arg(dstName).arg(dst->dimensions.y).arg(dst->dimensions.x);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Copy Map"),
        tr("AI wants to copy all values from <b>%1</b> to <b>%2</b> (%3×%4 cells).\n\nApply?")
            .arg(srcName).arg(dstName).arg(src->dimensions.y).arg(src->dimensions.x),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot(QString("AI copy %1 → %2").arg(srcName, dstName));

    const auto *constData = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    int dataLen = targetDataConst().size();
    int cols = src->dimensions.x;
    int rows = src->dimensions.y;
    uint32_t srcBase = src->address + src->mapDataOffset;
    uint32_t dstBase = dst->address + dst->mapDataOffset;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int srcIdx = src->columnMajor ? (c * rows + r) : (r * cols + c);
            int dstIdx = dst->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t srcOff = srcBase + (uint32_t)(srcIdx * src->dataSize);
            uint32_t dstOff = dstBase + (uint32_t)(dstIdx * dst->dataSize);

            uint32_t raw = readRomValue(constData, dataLen, srcOff, src->dataSize, m_project->byteOrder);
            // Convert through physical if data sizes differ
            if (src->dataSize != dst->dataSize || src->scaling.type != dst->scaling.type) {
                double phys = src->scaling.toPhysical(signExtendRaw(raw, src->dataSize, src->dataSigned));
                if (dst->dataSigned) {
                    double minS = (dst->dataSize == 1) ? -128.0 : (dst->dataSize == 4) ? -2147483648.0 : -32768.0;
                    double maxS = (dst->dataSize == 1) ? 127.0 : (dst->dataSize == 4) ? 2147483647.0 : 32767.0;
                    raw = (uint32_t)(int32_t)qBound(minS, std::round(dst->scaling.toRaw(phys)), maxS);
                } else {
                    raw = (uint32_t)qRound(dst->scaling.toRaw(phys));
                }
            }
            writeRomValue(rawData, dataLen, dstOff, dst->dataSize, m_project->byteOrder, raw);
        }
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]     = true;
    res["source"]      = srcName;
    res["destination"] = dstName;
    res["cellsCopied"] = rows * cols;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── write_rom_bytes ───────────────────────────────────────────────────────────

QString AIToolExecutor::toolWriteRomBytes(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    int offset = input["offset"].toInt(-1);
    QString hexStr = input["hex"].toString().remove(' ').remove('\n');

    if (hexStr.size() % 2 != 0) {
        QJsonObject err; err["error"] = "Hex string must have even length";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    QByteArray bytes = QByteArray::fromHex(hexStr.toLatin1());
    if (bytes.isEmpty()) {
        QJsonObject err; err["error"] = "Invalid hex string";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    if (bytes.size() > 64) {
        QJsonObject err; err["error"] = "Max 64 bytes per write_rom_bytes call";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    if (offset < 0 || offset + bytes.size() > targetDataConst().size()) {
        QJsonObject err; err["error"] = QString("Offset %1 + length %2 exceeds ROM size %3")
                                            .arg(offset).arg(bytes.size()).arg(targetDataConst().size());
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Show current vs proposed bytes
    QByteArray currentBytes = targetDataConst().mid(offset, bytes.size());
    QString curHex, newHex;
    for (int i = 0; i < bytes.size(); ++i) {
        if (i > 0) { curHex += ' '; newHex += ' '; }
        curHex += QString("%1").arg((uint8_t)currentBytes[i], 2, 16, QChar('0')).toUpper();
        newHex += QString("%1").arg((uint8_t)bytes[i], 2, 16, QChar('0')).toUpper();
    }

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Write ROM Bytes"),
        tr("AI wants to write %1 bytes at offset 0x%2:\n\n"
                "Current: %3\n"
                "New:     %4\n\n"
                "Apply this raw byte edit?")
            .arg(bytes.size())
            .arg((uint32_t)offset, 8, 16, QChar('0'))
            .arg(curHex, newHex),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot(QString("AI raw write at 0x%1").arg((uint32_t)offset, 8, 16, QChar('0')));
    std::memcpy(targetData().data() +offset, bytes.constData(), bytes.size());
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]      = true;
    res["offset"]       = offset;
    res["bytesWritten"] = bytes.size();
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── smooth_map ────────────────────────────────────────────────────────────────

QString AIToolExecutor::toolSmoothMap(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    int iterations = qBound(1, input["iterations"].toInt(1), 10);
    int cols = m->dimensions.x;
    int rows = m->dimensions.y;

    if (cols < 2 && rows < 2) {
        QJsonObject err; err["error"] = "Map is too small to smooth (single cell)";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Smooth Map"),
        tr("AI wants to smooth <b>%1</b> (%2×%3) with %4 iteration(s).\n\n"
                "Each cell will be averaged with its neighbors.\nApply?")
            .arg(mapName).arg(rows).arg(cols).arg(iterations),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot(QString("AI smooth %1 ×%2").arg(mapName).arg(iterations));

    const auto *constData = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    int dataLen = targetDataConst().size();
    uint32_t baseOff = mapOffsetInTarget(*m);

    // Read current physical values into 2D array
    QVector<QVector<double>> vals(rows, QVector<double>(cols));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            uint32_t raw = readRomValue(constData, dataLen, off, m->dataSize, m_project->byteOrder);
            vals[r][c] = m->scaling.toPhysical(signExtendRaw(raw, m->dataSize, m->dataSigned));
        }
    }

    // Apply smoothing passes
    for (int it = 0; it < iterations; ++it) {
        QVector<QVector<double>> smoothed(rows, QVector<double>(cols));
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                double sum = 0;
                int count = 0;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        int nr = r + dr, nc = c + dc;
                        if (nr >= 0 && nr < rows && nc >= 0 && nc < cols) {
                            // Center cell gets double weight
                            double w = (dr == 0 && dc == 0) ? 2.0 : 1.0;
                            sum += vals[nr][nc] * w;
                            count += (int)w;
                        }
                    }
                }
                smoothed[r][c] = sum / count;
            }
        }
        vals = smoothed;
    }

    // Write back
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
            uint32_t newRaw;
            if (m->dataSigned) {
                double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
                double maxS = (m->dataSize == 1) ? 127.0 : (m->dataSize == 4) ? 2147483647.0 : 32767.0;
                newRaw = (uint32_t)(int32_t)qBound(minS, std::round(m->scaling.toRaw(vals[r][c])), maxS);
            } else {
                newRaw = (uint32_t)qRound(m->scaling.toRaw(vals[r][c]));
            }
            writeRomValue(rawData, dataLen, off, m->dataSize, m_project->byteOrder, newRaw);
        }
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]      = true;
    res["map"]          = mapName;
    res["iterations"]   = iterations;
    res["cellsWritten"] = rows * cols;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── set_axis_values ───────────────────────────────────────────────────────────

QString AIToolExecutor::toolSetAxisValues(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    QString mapName = input["name"].toString();
    MapInfo *m = findMap(mapName);
    if (!m) return mapNotFound(mapName);

    QString axisStr = input["axis"].toString().toLower();
    bool isX = (axisStr == "x");
    bool isY = (axisStr == "y");
    if (!isX && !isY) {
        QJsonObject err; err["error"] = "axis must be 'x' or 'y'";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const AxisInfo &axis = isX ? m->xAxis : m->yAxis;
    int count = isX ? m->dimensions.x : m->dimensions.y;

    if (!axis.hasPtsAddress) {
        QJsonObject err; err["error"] = QString("%1 axis has no writable breakpoints (fixed axis)").arg(axisStr.toUpper());
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    QJsonArray valsArr = input["values"].toArray();
    int writeCount = qMin((int)valsArr.size(), count);

    // Read current values for display
    const auto *constData = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    QString curStr, newStr;
    for (int i = 0; i < writeCount; ++i) {
        uint32_t off = axis.ptsAddress + (uint32_t)(i * axis.ptsDataSize);
        uint32_t raw = readRomValue(constData, dataLen, off, axis.ptsDataSize, m_project->byteOrder);
        double curPhys = axis.scaling.toPhysical(signExtendRaw(raw, axis.ptsDataSize, axis.ptsSigned));
        double newPhys = valsArr[i].toDouble();
        if (i > 0) { curStr += ", "; newStr += ", "; }
        curStr += QString::number(curPhys, 'f', 2);
        newStr += QString::number(newPhys, 'f', 2);
    }

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Set Axis Values"),
        tr("AI wants to modify the <b>%1 axis</b> of <b>%2</b> (%3 breakpoints):\n\n"
                "Current: %4\n"
                "New:     %5\n\n"
                "Apply?")
            .arg(axisStr.toUpper(), mapName).arg(writeCount).arg(curStr, newStr),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot(QString("set %1 axis: %2").arg(axisStr.toUpper(), mapName));

    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    for (int i = 0; i < writeCount; ++i) {
        double phys = valsArr[i].toDouble();
        uint32_t raw;
        if (axis.ptsSigned) {
            double minS = (axis.ptsDataSize == 1) ? -128.0 : (axis.ptsDataSize == 4) ? -2147483648.0 : -32768.0;
            double maxS = (axis.ptsDataSize == 1) ? 127.0 : (axis.ptsDataSize == 4) ? 2147483647.0 : 32767.0;
            raw = (uint32_t)(int32_t)qBound(minS, std::round(axis.scaling.toRaw(phys)), maxS);
        } else {
            raw = (uint32_t)qRound(axis.scaling.toRaw(phys));
        }
        uint32_t off = axis.ptsAddress + (uint32_t)(i * axis.ptsDataSize);
        writeRomValue(rawData, dataLen, off, axis.ptsDataSize, m_project->byteOrder, raw);
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]       = true;
    res["map"]           = mapName;
    res["axis"]          = axisStr;
    res["pointsWritten"] = writeCount;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── undo_last_change ──────────────────────────────────────────────────────────

QString AIToolExecutor::toolUndoLastChange()
{
    if (!m_project) return noProjectError();

    if (m_project->versions.isEmpty()) {
        QJsonObject err; err["error"] = "No version snapshots available to undo to";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    int lastIdx = m_project->versions.size() - 1;
    const ProjectVersion &v = m_project->versions[lastIdx];

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Undo"),
        tr("Undo last change? This will restore to:\n\n\"%1\" (%2)\n\nContinue?")
            .arg(v.name, v.created.toString("HH:mm:ss")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    m_project->restoreVersion(lastIdx);
    emit projectModified();

    QJsonObject res;
    res["success"]  = true;
    res["restored"] = v.name;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── batch_zero_maps ───────────────────────────────────────────────────────────

QString AIToolExecutor::toolBatchZeroMaps(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    QString pattern = input["pattern"].toString().trimmed();
    if (pattern.isEmpty()) {
        QJsonObject err; err["error"] = "Empty pattern";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Match maps
    QString rxPattern = QRegularExpression::escape(pattern);
    rxPattern.replace("\\*", ".*").replace("\\?", ".");
    QRegularExpression rx("^" + rxPattern + "$", QRegularExpression::CaseInsensitiveOption);

    QVector<MapInfo*> matched;
    for (MapInfo &m : m_project->maps) {
        if (rx.match(m.name).hasMatch())
            matched.append(&m);
    }

    if (matched.isEmpty()) {
        QJsonObject err; err["error"] = "No maps matched pattern: " + pattern;
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Build confirmation list
    QString mapList;
    for (const MapInfo *m : matched)
        mapList += QString("  • %1 (%2×%3, %4)\n").arg(m->name).arg(m->dimensions.y).arg(m->dimensions.x).arg(m->scaling.unit);

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Batch Zero Maps"),
        tr("AI wants to zero <b>%1 maps</b> matching '%2':\n\n%3\nApply all?")
            .arg(matched.size()).arg(pattern).arg(mapList),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot("batch zero: " + pattern);

    auto *rawData = reinterpret_cast<uint8_t *>(targetData().data());
    int dataLen = targetData().size();
    int totalCells = 0;

    for (MapInfo *m : matched) {
        uint32_t baseOff = mapOffsetInTarget(*m);
        int cols = m->dimensions.x;
        int rows = m->dimensions.y;
        uint32_t rawZero;
        if (m->dataSigned) {
            double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
            double maxS = (m->dataSize == 1) ? 127.0 : (m->dataSize == 4) ? 2147483647.0 : 32767.0;
            rawZero = (uint32_t)(int32_t)qBound(minS, std::round(m->scaling.toRaw(0.0)), maxS);
        } else {
            rawZero = (uint32_t)qRound(m->scaling.toRaw(0.0));
        }

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int cellIdx = m->columnMajor ? (c * rows + r) : (r * cols + c);
                uint32_t off = baseOff + (uint32_t)(cellIdx * m->dataSize);
                writeRomValue(rawData, dataLen, off, m->dataSize, m_project->byteOrder, rawZero);
                ++totalCells;
            }
        }
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]    = true;
    res["pattern"]    = pattern;
    res["mapsZeroed"] = matched.size();
    res["totalCells"] = totalCells;
    QJsonArray names;
    for (const MapInfo *m : matched) names.append(m->name);
    res["maps"] = names;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── compare_two_maps ──────────────────────────────────────────────────────────

QString AIToolExecutor::toolCompareTwoMaps(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    QString name1 = input["map1"].toString();
    QString name2 = input["map2"].toString();
    MapInfo *m1 = findMap(name1);
    MapInfo *m2 = findMap(name2);
    if (!m1) return mapNotFound(name1);
    if (!m2) return mapNotFound(name2);

    if (m1->dimensions.x != m2->dimensions.x || m1->dimensions.y != m2->dimensions.y) {
        QJsonObject err;
        err["error"] = QString("Dimension mismatch: %1 is %2×%3 but %4 is %5×%6")
            .arg(name1).arg(m1->dimensions.y).arg(m1->dimensions.x)
            .arg(name2).arg(m2->dimensions.y).arg(m2->dimensions.x);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const auto *data = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    uint32_t base1 = mapOffsetInTarget(*m1);
    uint32_t base2 = mapOffsetInTarget(*m2);
    int cols = m1->dimensions.x;
    int rows = m1->dimensions.y;

    QJsonArray diffs;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cellIdx = m1->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off1 = base1 + (uint32_t)(cellIdx * m1->dataSize);
            int cellIdx2 = m2->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off2 = base2 + (uint32_t)(cellIdx2 * m2->dataSize);

            uint32_t raw1 = readRomValue(data, dataLen, off1, m1->dataSize, m_project->byteOrder);
            uint32_t raw2 = readRomValue(data, dataLen, off2, m2->dataSize, m_project->byteOrder);

            double phys1 = m1->scaling.toPhysical(signExtendRaw(raw1, m1->dataSize, m1->dataSigned));
            double phys2 = m2->scaling.toPhysical(signExtendRaw(raw2, m2->dataSize, m2->dataSigned));

            if (qAbs(phys1 - phys2) > 1e-9) {
                QJsonObject d;
                d["row"]  = r;
                d["col"]  = c;
                d["val1"] = phys1;
                d["val2"] = phys2;
                d["delta"]= phys2 - phys1;
                diffs.append(d);
            }
        }
    }

    QJsonObject res;
    res["map1"]         = name1;
    res["map2"]         = name2;
    res["unit1"]        = m1->scaling.unit;
    res["unit2"]        = m2->scaling.unit;
    res["totalCells"]   = cols * rows;
    res["changedCells"] = diffs.size();
    res["identical"]    = diffs.isEmpty();
    if (!diffs.isEmpty()) res["changes"] = diffs;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── snapshot_version ──────────────────────────────────────────────────────────

QString AIToolExecutor::toolSnapshotVersion(const QJsonObject &input)
{
    if (!m_project) {
        QJsonObject err; err["error"] = "No project loaded";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    QString label = input["label"].toString();
    if (label.isEmpty()) label = "AI snapshot";

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(), tr("AI Version Snapshot"),
        tr("AI wants to save a version snapshot:\n\n\"%1\"\n\nCreate this snapshot?").arg(label),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot(label);

    QJsonObject res;
    res["success"] = true;
    res["label"]   = label;
    res["versionCount"] = m_project->versions.size();
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── batch_modify_maps ──────────────────────────────────────────────────────

QString AIToolExecutor::toolBatchModifyMaps(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    QJsonArray ops = input["operations"].toArray();
    QString reason = input["reason"].toString("AI batch modification");

    if (ops.isEmpty()) {
        QJsonObject err; err["error"] = "No operations specified";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Phase 1: Validate all operations
    struct ValidatedOp {
        MapInfo *map;
        QString action;
        double value;
    };
    QVector<ValidatedOp> validated;
    QStringList errors;

    for (const auto &opVal : ops) {
        QJsonObject op = opVal.toObject();
        QString mapName = op["map_name"].toString();
        const QString mapAddress = op["map_address"].toString().trimmed();
        if (!mapAddress.isEmpty()) mapName = "@" + mapAddress;
        QString action = op["action"].toString();
        double value = op["value"].toDouble(0);

        MapInfo *m = findMap(mapName);
        if (!m) { errors.append(QString("Map '%1' not found or ambiguous; provide map_address for duplicate names").arg(mapName)); continue; }

        QStringList validActions = {"zero", "fill", "scale", "offset", "restore"};
        if (!validActions.contains(action)) { errors.append(QString("Invalid action '%1' for '%2'").arg(action, mapName)); continue; }

        validated.append({m, action, value});
    }

    if (validated.isEmpty()) {
        QJsonObject err; err["error"] = QString("All operations failed validation: %1").arg(errors.join("; "));
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Phase 2: Build preview summary
    QString preview;
    for (const auto &vop : validated) {
        preview += QString::fromUtf8("• ") + QString("%1: %2").arg(vop.map->name, vop.action);
        if (vop.action == "fill") preview += QString(" (value=%1)").arg(vop.value);
        else if (vop.action == "scale") preview += QString(" (factor=%1)").arg(vop.value);
        else if (vop.action == "offset") preview += QString(" (delta=%1)").arg(vop.value);
        preview += "\n";
    }

    // Phase 3: Single confirmation
    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(),
        tr("AI Batch Modification"),
        tr("The AI wants to modify %1 map(s):\n\n%2\nReason: %3\n\nApprove all changes?")
            .arg(validated.size()).arg(preview).arg(reason),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject err; err["error"] = "User cancelled batch modification";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Phase 4: Single version snapshot
    ensureVersionSnapshot(reason);

    // Phase 5: Apply all operations
    int applied = 0;
    QJsonArray results;

    for (const auto &vop : validated) {
        QByteArray &data = targetData();
        uint32_t offset = mapOffsetInTarget(*vop.map);
        int cellSize = vop.map->dataSize > 0 ? vop.map->dataSize : 2;
        bool ok = true;
        int dx = vop.map->dimensions.x;
        int dy = vop.map->dimensions.y;
        if (dx <= 0 || dy <= 0 || dx > 10000 || dy > 10000) { ok = false; continue; }
        int totalCells = dx * dy;
        if (totalCells > 1000000) { ok = false; continue; }

        if (vop.action == "zero") {
            for (int i = 0; i < totalCells * cellSize; ++i)
                data[offset + i] = 0;
        } else if (vop.action == "fill") {
            uint32_t raw = (uint32_t)(int32_t)vop.value;
            for (int i = 0; i < totalCells; ++i) {
                if (cellSize == 1) data[offset + i] = (uint8_t)(raw & 0xFF);
                else if (cellSize == 2) {
                    data[offset + i*2] = (uint8_t)((raw >> 8) & 0xFF);
                    data[offset + i*2 + 1] = (uint8_t)(raw & 0xFF);
                }
            }
        } else if (vop.action == "scale") {
            for (int i = 0; i < totalCells; ++i) {
                uint32_t raw = 0;
                for (int b = 0; b < cellSize; ++b)
                    raw = (raw << 8) | (uint8_t)data[offset + i*cellSize + b];
                raw = (uint32_t)(raw * vop.value);
                for (int b = cellSize - 1; b >= 0; --b) {
                    data[offset + i*cellSize + b] = (uint8_t)(raw & 0xFF);
                    raw >>= 8;
                }
            }
        } else if (vop.action == "offset") {
            for (int i = 0; i < totalCells; ++i) {
                uint32_t raw = 0;
                for (int b = 0; b < cellSize; ++b)
                    raw = (raw << 8) | (uint8_t)data[offset + i*cellSize + b];
                raw = (uint32_t)((int32_t)raw + (int32_t)vop.value);
                for (int b = cellSize - 1; b >= 0; --b) {
                    data[offset + i*cellSize + b] = (uint8_t)(raw & 0xFF);
                    raw >>= 8;
                }
            }
        } else if (vop.action == "restore") {
            if (offset + totalCells * cellSize <= (uint32_t)m_project->originalData.size()) {
                for (int i = 0; i < totalCells * cellSize; ++i)
                    data[offset + i] = m_project->originalData[offset + i];
            } else ok = false;
        }

        if (ok) {
            ++applied;
            results.append(QJsonObject{{"map", vop.map->name}, {"action", vop.action}, {"status", "ok"}});
        } else {
            results.append(QJsonObject{{"map", vop.map->name}, {"action", vop.action}, {"status", "failed"}});
        }
    }

    if (applied > 0) {
        m_project->modified = true;
        emit projectModified();
    }

    QJsonObject result;
    result["applied"] = applied;
    result["total"] = validated.size();
    result["reason"] = reason;
    result["results"] = results;
    if (!errors.isEmpty()) result["validation_errors"] = QJsonArray::fromStringList(errors);
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Analysis tools
// ═══════════════════════════════════════════════════════════════════════════════

// ── describe_map_shape ────────────────────────────────────────────────────────

QString AIToolExecutor::toolDescribeMapShape(const QJsonObject &input)
{
    if (!m_project) return noProjectError();
    MapInfo *m = findMap(input["map_name"].toString());
    if (!m) return mapNotFound(input["map_name"].toString());

    const auto *data = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    uint32_t base = mapOffsetInTarget(*m);
    int cols = m->dimensions.x;
    int rows = m->dimensions.y;
    if (cols <= 0 || rows <= 0) {
        QJsonObject err; err["error"] = "Map has invalid dimensions";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Read all physical values
    QVector<QVector<double>> vals(rows, QVector<double>(cols));
    double vMin = 1e18, vMax = -1e18, vSum = 0;
    int total = rows * cols;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = m->columnMajor ? (c * rows + r) : (r * cols + c);
            uint32_t off = base + (uint32_t)(idx * m->dataSize);
            uint32_t raw = readRomValue(data, dataLen, off, m->dataSize, m_project->byteOrder);
            double phys = m->scaling.toPhysical(signExtendRaw(raw, m->dataSize, m->dataSigned));
            vals[r][c] = phys;
            vMin = qMin(vMin, phys);
            vMax = qMax(vMax, phys);
            vSum += phys;
        }
    }
    double vMean = vSum / total;
    double range = vMax - vMin;

    // Count flat cells (within 1% of range from mean)
    int flatCount = 0;
    double flatThresh = range * 0.01 + 1e-9;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if (qAbs(vals[r][c] - vMean) < flatThresh) ++flatCount;

    double flatPct = 100.0 * flatCount / total;
    bool isFlat = flatPct > 80.0;

    // Check monotonicity along both axes (only meaningful for 2D+ maps)
    bool xMonoInc = true, xMonoDec = true, yMonoInc = true, yMonoDec = true;
    if (cols > 1 && rows > 0) {
        for (int r = 0; r < rows; ++r) {
            for (int c = 1; c < cols; ++c) {
                if (vals[r][c] < vals[r][c-1]) xMonoInc = false;
                if (vals[r][c] > vals[r][c-1]) xMonoDec = false;
            }
        }
    }
    if (rows > 1 && cols > 0) {
        for (int c = 0; c < cols; ++c) {
            for (int r = 1; r < rows; ++r) {
                if (vals[r][c] < vals[r-1][c]) yMonoInc = false;
                if (vals[r][c] > vals[r-1][c]) yMonoDec = false;
            }
        }
    }

    // Find peak and valley cells
    double peakVal = vMax, valleyVal = vMin;
    int peakRow = 0, peakCol = 0, valRow = 0, valCol = 0;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            if (vals[r][c] >= peakVal) { peakVal = vals[r][c]; peakRow = r; peakCol = c; }
            if (vals[r][c] <= valleyVal) { valleyVal = vals[r][c]; valRow = r; valCol = c; }
        }

    // Compute avg gradient magnitude
    double gradSum = 0; int gradCount = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c + 1 < cols; ++c) {
            gradSum += qAbs(vals[r][c+1] - vals[r][c]);
            ++gradCount;
        }
    }
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r + 1 < rows; ++r) {
            gradSum += qAbs(vals[r+1][c] - vals[r][c]);
            ++gradCount;
        }
    }
    double avgGrad = gradCount > 0 ? gradSum / gradCount : 0;

    // Detect cliff (large single-step jump)
    double maxStep = 0;
    for (int r = 0; r < rows; ++r)
        for (int c = 1; c < cols; ++c)
            maxStep = qMax(maxStep, qAbs(vals[r][c] - vals[r][c-1]));
    for (int c = 0; c < cols; ++c)
        for (int r = 1; r < rows; ++r)
            maxStep = qMax(maxStep, qAbs(vals[r][c] - vals[r-1][c]));
    bool hasCliff = range > 1e-9 && maxStep / range > 0.5;

    // Build shape description
    QStringList traits;
    if (isFlat)          traits << "flat (all values near constant)";
    if (xMonoInc && !isFlat) traits << "monotonically increasing along X";
    if (xMonoDec && !isFlat) traits << "monotonically decreasing along X";
    if (yMonoInc && !isFlat) traits << "monotonically increasing along Y";
    if (yMonoDec && !isFlat) traits << "monotonically decreasing along Y";
    if (hasCliff)        traits << QString("has a sharp cliff (max step %1 %2)").arg(maxStep, 0, 'f', 2).arg(m->scaling.unit);
    if (traits.isEmpty()) traits << "non-monotonic with varying gradient";

    QJsonObject res;
    res["map"]         = m->name;
    res["rows"]        = rows;
    res["cols"]        = cols;
    res["unit"]        = m->scaling.unit;
    res["min"]         = vMin;
    res["max"]         = vMax;
    res["mean"]        = vMean;
    res["range"]       = range;
    res["avg_gradient"]= avgGrad;
    res["flat_pct"]    = flatPct;
    res["peak"]        = QJsonObject{{"row", peakRow}, {"col", peakCol}, {"value", peakVal}};
    res["valley"]      = QJsonObject{{"row", valRow}, {"col", valCol}, {"value", valleyVal}};
    res["shape"]       = traits.join("; ");
    res["x_monotone_inc"] = xMonoInc;
    res["x_monotone_dec"] = xMonoDec;
    res["y_monotone_inc"] = yMonoInc;
    res["y_monotone_dec"] = yMonoDec;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── get_related_maps ──────────────────────────────────────────────────────────

QString AIToolExecutor::toolGetRelatedMaps(const QJsonObject &input)
{
    if (!m_project) return noProjectError();
    const QString name = input["map_name"].toString();
    MapInfo *target = findMap(name);
    if (!target) return mapNotFound(name);

    // Find the group this map belongs to
    QString groupName;
    for (const A2LGroup &g : m_project->groups) {
        if (g.characteristics.contains(name)) { groupName = g.name; break; }
    }

    // Shared axis signal search
    QStringList xSig = target->xAxis.inputName.isEmpty() ? QStringList() : QStringList{target->xAxis.inputName};
    QStringList ySig = target->yAxis.inputName.isEmpty() ? QStringList() : QStringList{target->yAxis.inputName};

    // Name prefix (first 4 chars)
    QString prefix = name.length() >= 4 ? name.left(4) : name;

    struct RelMap { const MapInfo *map; QString reason; };
    QVector<RelMap> related;
    QSet<uint32_t> seen; seen.insert(target->rawAddress);

    for (const MapInfo &m : m_project->maps) {
        if (seen.contains(m.rawAddress)) continue;
        QStringList reasons;

        // Same group
        if (!groupName.isEmpty()) {
            for (const A2LGroup &g : m_project->groups)
                if (g.name == groupName && g.characteristics.contains(m.name))
                    reasons << "same A2L group: " + groupName;
        }
        // Shared axis signal
        if (!xSig.isEmpty() && m.xAxis.inputName == xSig[0])
            reasons << "shared X-axis signal: " + xSig[0];
        if (!ySig.isEmpty() && m.yAxis.inputName == ySig[0])
            reasons << "shared Y-axis signal: " + ySig[0];
        // Same name prefix
        if (m.name.startsWith(prefix, Qt::CaseInsensitive) && prefix.length() >= 3)
            reasons << "name prefix match: " + prefix;
        // Same dimensions
        if (m.dimensions.x == target->dimensions.x && m.dimensions.y == target->dimensions.y
            && target->dimensions.x > 1)
            reasons << QString("same dimensions %1×%2").arg(target->dimensions.y).arg(target->dimensions.x);

        if (!reasons.isEmpty()) {
            seen.insert(m.rawAddress);
            related.append({&m, reasons.join(", ")});
        }
        if (related.size() >= 30) break;
    }

    QJsonArray arr;
    for (const auto &rm : related) {
        QJsonObject item{{"name", rm.map->name}, {"reason", rm.reason}};
        addMapIdentity(item, *rm.map);
        arr.append(item);
    }

    QJsonObject res;
    res["map"]   = name;
    res["group"] = groupName;
    res["x_axis_signal"] = target->xAxis.inputName;
    res["y_axis_signal"] = target->yAxis.inputName;
    res["related_count"] = related.size();
    res["related"] = arr;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── identify_map_purpose ──────────────────────────────────────────────────────

QString AIToolExecutor::toolIdentifyMapPurpose(const QJsonObject &input)
{
    if (!m_project) return noProjectError();
    const QString name = input["map_name"].toString();
    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    // Pattern database: {name_pattern, purpose, category, confidence}
    struct Pattern {
        QString pattern;   // wildcard-style
        QString purpose;
        QString category;
        QString confidence;
    };
    static const QVector<Pattern> patterns = {
        {"KAT*",     "Catalyst monitoring threshold",          "emissions",     "high"},
        {"DKAT*",    "Catalyst diagnostic counter/flag",       "emissions",     "high"},
        {"DFC*KAT*", "Catalyst DTC fault code mask",           "diagnostics",   "high"},
        {"OSCKAT*",  "Catalyst oscillation monitor",           "emissions",     "high"},
        {"DPF*",     "DPF (Diesel Particulate Filter) control","emissions",     "high"},
        {"RUSS*",    "Soot/particulate accumulation model",    "emissions",     "high"},
        {"PARMON*",  "Particulate monitor",                    "emissions",     "high"},
        {"AGR*",     "EGR (Exhaust Gas Recirculation) control","emissions",     "high"},
        {"DAGR*",    "EGR desired/target position",            "emissions",     "high"},
        {"LSMON*",   "Lambda sensor monitoring",               "emissions",     "high"},
        {"LSHFM*",   "Lambda heating monitor",                 "emissions",     "high"},
        {"LAMFA*",   "Lambda adaptation factor",               "fuel_control",  "high"},
        {"VMAX*",    "Maximum vehicle speed limit",            "speed_limiter", "high"},
        {"NMAX*",    "Maximum engine speed (rev limiter)",     "rev_limiter",   "high"},
        {"NMOT_MAX*","Maximum engine speed override",          "rev_limiter",   "high"},
        {"SAK*",     "Overrun fuel cut / ignition delay",      "pops_bangs",    "high"},
        {"KFZWSA*",  "Ignition retard map (overrun)",          "pops_bangs",    "high"},
        {"SCR*",     "SCR/AdBlue catalyst control",            "emissions",     "high"},
        {"ADBLUE*",  "AdBlue dosing",                          "emissions",     "high"},
        {"KFPED*",   "Pedal characteristic / e-throttle map",  "driver_demand", "high"},
        {"KFURL*",   "Throttle request map",                   "driver_demand", "medium"},
        {"KFMSNWDK*","Torque demand map",                      "torque_model",  "medium"},
        {"KFMS*",    "Torque-related map",                     "torque_model",  "medium"},
        {"LDRK*",    "Boost pressure control map",             "boost",         "high"},
        {"LDTVM*",   "Turbo wastegate control",                "boost",         "high"},
        {"DFC*",     "Diagnostic fault code threshold/mask",   "diagnostics",   "medium"},
        {"MLHFM*",   "Air mass flow meter adaptation",         "fuel_control",  "high"},
        {"FKKVS*",   "Fuel injector correction factor",        "fuel_control",  "medium"},
        {"KFNWS*",   "Idle speed setpoint map",                "idle",          "medium"},
        {"TANS*",    "Ambient temperature sensor model",       "temperature",   "medium"},
        {"IMMO*",    "Immobilizer control",                    "security",      "high"},
    };

    // Match by wildcard
    QString bestPurpose, bestCategory, bestConf;
    for (const Pattern &p : patterns) {
        QString rxPat = QRegularExpression::escape(p.pattern);
        rxPat.replace("\\*", ".*").replace("\\?", ".");
        QRegularExpression rx("^" + rxPat + "$", QRegularExpression::CaseInsensitiveOption);
        if (rx.match(name).hasMatch()) {
            bestPurpose  = p.purpose;
            bestCategory = p.category;
            bestConf     = p.confidence;
            break;
        }
    }

    // Fallback: infer from unit
    if (bestPurpose.isEmpty()) {
        const QString &unit = m->scaling.unit.toLower();
        if (unit.contains("rpm"))       { bestPurpose = "Engine speed related map";   bestConf = "low"; }
        else if (unit.contains("bar") || unit.contains("kpa")) { bestPurpose = "Pressure map"; bestConf = "low"; }
        else if (unit.contains("mg/hub") || unit.contains("mg/cyl")) { bestPurpose = "Fuel quantity / injection map"; bestConf = "low"; }
        else if (unit.contains("°c") || unit.contains("deg"))  { bestPurpose = "Temperature map"; bestConf = "low"; }
        else if (unit.contains("nm"))   { bestPurpose = "Torque map"; bestConf = "low"; }
        else if (unit.contains("lambda")|| unit.contains("afr")) { bestPurpose = "Lambda / air-fuel ratio map"; bestConf = "low"; }
        else if (unit.contains("%"))    { bestPurpose = "Percentage / duty cycle map"; bestConf = "low"; }
        else { bestPurpose = "Unknown purpose — check A2L description"; bestConf = "unknown"; }
        bestCategory = "unknown";
    }

    QJsonObject res;
    res["map"]         = name;
    res["purpose"]     = bestPurpose;
    res["category"]    = bestCategory;
    res["confidence"]  = bestConf;
    res["unit"]        = m->scaling.unit;
    res["description"] = m->description;
    res["type"]        = m->type;
    res["dimensions"]  = QString("%1 rows × %2 cols").arg(m->dimensions.y).arg(m->dimensions.x);
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── validate_map_changes ──────────────────────────────────────────────────────

QString AIToolExecutor::toolValidateMapChanges(const QJsonObject &input)
{
    if (!m_project) return noProjectError();
    const QString name = input["map_name"].toString();
    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    QJsonArray rowsArr = input["proposed_values"].toArray();
    if (rowsArr.isEmpty()) {
        QJsonObject err; err["error"] = "proposed_values is empty";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Physics-based unit bounds
    struct UnitBounds { QString unitFragment; double minVal; double maxVal; QString reason; };
    static const QVector<UnitBounds> bounds = {
        {"rpm",      0,      12000, "Engine RPM range"},
        {"bar",      0,      5,     "Boost pressure range"},
        {"kpa",      0,      500,   "Pressure kPa range"},
        {"mg/hub",   0,      200,   "Fuel injection quantity range"},
        {"mg/cyl",   0,      200,   "Fuel injection quantity range"},
        {"nm",       -100,   2000,  "Torque range"},
        {"°c",       -50,    1200,  "Temperature range (°C)"},
        {"deg",      -90,    90,    "Angle/ignition degree range"},
        {"%",        -200,   200,   "Percentage range"},
        {"lambda",   0.5,    2.0,   "Lambda range"},
        {"afr",      8,      22,    "Air-fuel ratio range"},
        {"v",        0,      16,    "Voltage range"},
    };

    QString unitLower = m->scaling.unit.toLower();
    double hardMin = -1e12, hardMax = 1e12;
    QString boundsReason;
    for (const UnitBounds &ub : bounds) {
        if (unitLower.contains(ub.unitFragment)) {
            hardMin = ub.minVal;
            hardMax = ub.maxVal;
            boundsReason = ub.reason;
            break;
        }
    }

    // Check each cell
    QJsonArray violations;
    int checkedCells = 0;

    for (int r = 0; r < rowsArr.size(); ++r) {
        QJsonArray colArr = rowsArr[r].toArray();
        for (int c = 0; c < colArr.size(); ++c) {
            double v = colArr[c].toDouble();
            ++checkedCells;
            if (v < hardMin || v > hardMax) {
                violations.append(QJsonObject{
                    {"row", r}, {"col", c}, {"value", v},
                    {"reason", QString("out of range [%1, %2] %3").arg(hardMin).arg(hardMax).arg(m->scaling.unit)}
                });
            }
        }
    }

    QJsonObject res;
    res["map"]           = name;
    res["unit"]          = m->scaling.unit;
    res["checked_cells"] = checkedCells;
    res["violations"]    = violations.size();
    res["safe"]          = violations.isEmpty();
    if (!boundsReason.isEmpty()) res["bounds_applied"] = boundsReason;
    if (!violations.isEmpty()) res["violation_list"] = violations;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── detect_anomalies ─────────────────────────────────────────────────────────

QString AIToolExecutor::toolDetectAnomalies(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    const QString filterName = input["map_name"].toString();
    QVector<MapInfo*> toScan;

    if (!filterName.isEmpty()) {
        MapInfo *m = findMap(filterName);
        if (!m) return mapNotFound(filterName);
        toScan.append(m);
    } else {
        // Scan all modified maps
        const auto *orig = reinterpret_cast<const uint8_t *>(m_project->originalData.constData());
        const auto *curr = reinterpret_cast<const uint8_t *>(m_project->currentData.constData());
        int sz = qMin(m_project->originalData.size(), m_project->currentData.size());
        for (MapInfo &m : m_project->maps) {
            uint32_t off = m.address;
            int len = m.length;
            if ((int)off + len > sz) continue;
            if (memcmp(orig + off, curr + off, len) != 0) toScan.append(&m);
        }
        if (toScan.isEmpty()) {
            QJsonObject res;
            res["anomalies"]  = 0;
            res["message"]    = "No modified maps to scan.";
            return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
        }
    }

    const auto *data = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();

    QJsonArray anomalies;

    for (MapInfo *m : toScan) {
        uint32_t base = mapOffsetInTarget(*m);
        int cols = m->dimensions.x, rows = m->dimensions.y;
        if (cols <= 0 || rows <= 0) continue;

        double vMin = 1e18, vMax = -1e18, vSum = 0;
        int total = cols * rows;
        QVector<double> vals(total);

        for (int i = 0; i < total; ++i) {
            uint32_t off = base + (uint32_t)(i * m->dataSize);
            uint32_t raw = readRomValue(data, dataLen, off, m->dataSize, m_project->byteOrder);
            double phys = m->scaling.toPhysical(signExtendRaw(raw, m->dataSize, m->dataSigned));
            vals[i] = phys;
            vMin = qMin(vMin, phys);
            vMax = qMax(vMax, phys);
            vSum += phys;
        }
        double vMean = vSum / total;
        double range = vMax - vMin;

        // Anomaly 1: Completely flat on a map that should have variation
        bool isFlat = range < 1e-9;
        if (isFlat && total > 4)
            anomalies.append(QJsonObject{{"map", m->name},
                {"type", "flat_map"}, {"detail", "All values identical — map may be disabled or zeroed"}});

        // Anomaly 2: Outlier cells (value > 3 stdev from mean)
        double vSumSq = 0;
        for (double v : vals) vSumSq += (v - vMean) * (v - vMean);
        double stdev = total > 1 ? std::sqrt(vSumSq / total) : 0;
        if (stdev > 1e-9) {
            int outliers = 0;
            for (double v : vals)
                if (qAbs(v - vMean) > 3 * stdev) ++outliers;
            if (outliers > 0)
                anomalies.append(QJsonObject{{"map", m->name},
                    {"type", "outlier_cells"}, {"count", outliers},
                    {"detail", QString("%1 cell(s) are >3σ from mean (%2 %3)").arg(outliers).arg(vMean, 0, 'f', 2).arg(m->scaling.unit)}});
        }

        // Anomaly 3: Values at raw integer limit (likely corrupted or uninitialized)
        uint32_t rawMax = (m->dataSize == 1) ? 0xFF : (m->dataSize == 4) ? 0xFFFFFFFF : 0xFFFF;
        double physMax = m->scaling.toPhysical(signExtendRaw(rawMax, m->dataSize, m->dataSigned));
        double physMin2 = m->dataSigned ? m->scaling.toPhysical(signExtendRaw(rawMax / 2 + 1, m->dataSize, true)) : 0;
        int limitCells = 0;
        for (int i = 0; i < total; ++i) {
            uint32_t off = base + (uint32_t)(i * m->dataSize);
            uint32_t raw = readRomValue(data, dataLen, off, m->dataSize, m_project->byteOrder);
            if (raw == rawMax || (m->dataSigned && raw == (rawMax / 2 + 1)))
                ++limitCells;
        }
        if (limitCells > 0)
            anomalies.append(QJsonObject{{"map", m->name},
                {"type", "raw_limit"}, {"count", limitCells},
                {"detail", QString("%1 cell(s) are at raw integer limit (possible overflow)").arg(limitCells)}});
    }

    QJsonObject res;
    res["scanned"]   = toScan.size();
    res["anomalies"] = anomalies.size();
    if (!anomalies.isEmpty()) res["findings"] = anomalies;
    else res["message"] = "No anomalies detected.";
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── summarize_all_differences ─────────────────────────────────────────────────

QString AIToolExecutor::toolSummarizeAllDifferences()
{
    if (!m_project) return noProjectError();
    if (m_project->originalData.isEmpty()) {
        QJsonObject err; err["error"] = "No original ROM data available for comparison";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const auto *orig = reinterpret_cast<const uint8_t *>(m_project->originalData.constData());
    const auto *curr = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int sz = qMin(m_project->originalData.size(), targetDataConst().size());

    QJsonArray mapDiffs;
    int totalMapsChanged = 0;
    int totalCellsChanged = 0;

    for (const MapInfo &m : m_project->maps) {
        uint32_t off = mapOffsetInTarget(m);
        int cols = m.dimensions.x, rows = m.dimensions.y;
        if (cols <= 0 || rows <= 0) continue;
        int total = cols * rows;
        if ((int)off + total * m.dataSize > sz) continue;

        double minDelta = 1e18, maxDelta = -1e18, sumDelta = 0;
        int changedCells = 0;

        for (int i = 0; i < total; ++i) {
            uint32_t rawOrig = readRomValue(orig, sz, off + (uint32_t)(i * m.dataSize), m.dataSize, m_project->byteOrder);
            uint32_t rawCurr = readRomValue(curr, sz, off + (uint32_t)(i * m.dataSize), m.dataSize, m_project->byteOrder);
            if (rawOrig == rawCurr) continue;
            ++changedCells;
            double dOrig = m.scaling.toPhysical(signExtendRaw(rawOrig, m.dataSize, m.dataSigned));
            double dCurr = m.scaling.toPhysical(signExtendRaw(rawCurr, m.dataSize, m.dataSigned));
            double delta = dCurr - dOrig;
            minDelta = qMin(minDelta, delta);
            maxDelta = qMax(maxDelta, delta);
            sumDelta += delta;
        }

        if (changedCells > 0) {
            ++totalMapsChanged;
            totalCellsChanged += changedCells;
            mapDiffs.append(QJsonObject{
                {"map",           m.name},
                {"cells_changed", changedCells},
                {"total_cells",   total},
                {"unit",          m.scaling.unit},
                {"min_delta",     minDelta},
                {"max_delta",     maxDelta},
                {"avg_delta",     sumDelta / changedCells}
            });
        }
    }

    QJsonObject res;
    res["maps_changed"]  = totalMapsChanged;
    res["cells_changed"] = totalCellsChanged;
    res["maps"]          = mapDiffs;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── get_tuning_notes ──────────────────────────────────────────────────────────

QString AIToolExecutor::toolGetTuningNotes(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    const QString filterMap = input["map_name"].toString();
    const int limit = qMax(1, input["limit"].toInt(20));

    QJsonArray entries;
    int count = 0;

    // Walk log in reverse (most recent first)
    for (int i = m_project->tuningLog.size() - 1; i >= 0 && count < limit; --i) {
        const TuningLogEntry &e = m_project->tuningLog[i];
        if (!filterMap.isEmpty() && !e.mapName.isEmpty() && e.mapName != filterMap) continue;
        QJsonObject obj;
        obj["timestamp"] = e.timestamp.toString(Qt::ISODate);
        obj["author"]    = e.author;
        obj["category"]  = e.category;
        obj["message"]   = e.message;
        if (!e.mapName.isEmpty()) obj["map"]  = e.mapName;
        if (!e.before.isEmpty()) obj["before"] = e.before;
        if (!e.after.isEmpty())  obj["after"]  = e.after;
        entries.append(obj);
        ++count;
    }

    // Also include dyno runs if no map filter
    if (filterMap.isEmpty()) {
        for (int i = m_project->dynoLog.size() - 1; i >= 0 && count < limit; --i) {
            const DynoResult &d = m_project->dynoLog[i];
            QJsonObject obj;
            obj["timestamp"]   = d.timestamp.toString(Qt::ISODate);
            obj["category"]    = "dyno";
            obj["peak_power"]  = d.peakPower;
            obj["power_unit"]  = d.powerUnit;
            obj["peak_torque"] = d.peakTorque;
            obj["rpm"]         = d.rpmAtPower;
            if (!d.notes.isEmpty()) obj["notes"] = d.notes;
            entries.append(obj);
            ++count;
        }
    }

    QJsonObject res;
    res["total_log_entries"] = m_project->tuningLog.size();
    res["dyno_runs"]         = m_project->dynoLog.size();
    res["returned"]          = entries.size();
    res["entries"]           = entries;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── confidence_search ─────────────────────────────────────────────────────────

QString AIToolExecutor::toolConfidenceSearch(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    const QString query = input["query"].toString().trimmed();
    if (query.isEmpty()) {
        QJsonObject err; err["error"] = "Empty query";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const QString qLower = query.toLower();

    struct Scored { MapInfo *m; int score; };
    QVector<Scored> results;

    for (MapInfo &m : m_project->maps) {
        int score = 0;
        const QString nameLower = m.name.toLower();
        const QString descLower = m.description.toLower();

        // Exact name match
        if (nameLower == qLower) score += 100;
        // Name starts with query
        else if (nameLower.startsWith(qLower)) score += 70;
        // Name contains query
        else if (nameLower.contains(qLower)) score += 50;
        // Description contains query
        if (descLower.contains(qLower)) score += 30;
        // Unit contains query
        if (m.scaling.unit.toLower().contains(qLower)) score += 20;
        // Wildcard pattern match
        {
            QString pat = QRegularExpression::escape(query);
            pat.replace("\\*", ".*").replace("\\?", ".");
            QRegularExpression rx("^" + pat + "$", QRegularExpression::CaseInsensitiveOption);
            if (rx.match(m.name).hasMatch()) score = qMax(score, 80);
        }

        if (score > 0) results.append({&m, score});
    }

    // Sort by score descending
    std::sort(results.begin(), results.end(), [](const Scored &a, const Scored &b){ return a.score > b.score; });

    QJsonArray arr;
    for (const auto &s : results) {
        QJsonObject item{
            {"name",        s.m->name},
            {"description", s.m->description},
            {"confidence",  s.score},
            {"type",        s.m->type},
            {"unit",        s.m->scaling.unit},
            {"dimensions",  QString("%1×%2").arg(s.m->dimensions.y).arg(s.m->dimensions.x)}
        };
        addMapIdentity(item, *s.m);
        arr.append(item);
    }

    QJsonObject res;
    res["query"]   = query;
    res["count"]   = arr.size();
    res["results"] = arr;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════════
// New write tools
// ═══════════════════════════════════════════════════════════════════════════════

// ── evaluate_map_expression ───────────────────────────────────────────────────
// Simple expression evaluator: variables v, r, c; operators + - * / pow abs min max

namespace {

// Token types for the expression evaluator
enum class TokType { Num, Plus, Minus, Star, Slash, LParen, RParen, Comma,
                     Var_v, Var_r, Var_c, Func, End, Unknown };
struct Token { TokType type; double num = 0; QString name; };

// Tokenizer
static QVector<Token> tokenize(const QString &expr, bool &ok)
{
    QVector<Token> toks;
    int i = 0;
    while (i < expr.size()) {
        QChar ch = expr[i];
        if (ch.isSpace()) { ++i; continue; }
        if (ch.isDigit() || (ch == '.' && i+1 < expr.size() && expr[i+1].isDigit())) {
            int start = i;
            while (i < expr.size() && (expr[i].isDigit() || expr[i] == '.')) ++i;
            bool numOk;
            double val = expr.mid(start, i - start).toDouble(&numOk);
            if (!numOk) { ok = false; return {}; }
            toks.append({TokType::Num, val, {}});
            continue;
        }
        if (ch.isLetter() || ch == '_') {
            int start = i;
            while (i < expr.size() && (expr[i].isLetterOrNumber() || expr[i] == '_')) ++i;
            QString word = expr.mid(start, i - start).toLower();
            if (word == "v") toks.append({TokType::Var_v});
            else if (word == "r") toks.append({TokType::Var_r});
            else if (word == "c") toks.append({TokType::Var_c});
            else if (word == "pow" || word == "min" || word == "max" || word == "abs"
                  || word == "sqrt" || word == "floor" || word == "ceil" || word == "round")
                toks.append({TokType::Func, 0, word});
            else { ok = false; return {}; }
            continue;
        }
        if (ch == '+') toks.append({TokType::Plus});
        else if (ch == '-') toks.append({TokType::Minus});
        else if (ch == '*') toks.append({TokType::Star});
        else if (ch == '/') toks.append({TokType::Slash});
        else if (ch == '(') toks.append({TokType::LParen});
        else if (ch == ')') toks.append({TokType::RParen});
        else if (ch == ',') toks.append({TokType::Comma});
        else { ok = false; return {}; }
        ++i;
    }
    toks.append({TokType::End});
    return toks;
}

// Recursive-descent parser
struct ExprParser {
    const QVector<Token> &toks;
    int pos = 0;
    double v = 0, r = 0, c = 0;
    bool ok = true;

    Token &cur() { return const_cast<Token&>(toks[pos]); }
    Token consume() { return toks[pos++]; }

    double parseExpr() { return parseAddSub(); }

    double parseAddSub() {
        double left = parseMulDiv();
        while (ok && (cur().type == TokType::Plus || cur().type == TokType::Minus)) {
            bool plus = consume().type == TokType::Plus;
            double right = parseMulDiv();
            left = plus ? left + right : left - right;
        }
        return left;
    }

    double parseMulDiv() {
        double left = parseUnary();
        while (ok && (cur().type == TokType::Star || cur().type == TokType::Slash)) {
            bool mul = consume().type == TokType::Star;
            double right = parseUnary();
            if (!mul && qAbs(right) < 1e-300) { ok = false; return 0; }
            left = mul ? left * right : left / right;
        }
        return left;
    }

    double parseUnary() {
        if (cur().type == TokType::Minus) { consume(); return -parsePrimary(); }
        if (cur().type == TokType::Plus)  { consume(); return  parsePrimary(); }
        return parsePrimary();
    }

    double parsePrimary() {
        if (!ok) return 0;
        if (cur().type == TokType::Num)   { return consume().num; }
        if (cur().type == TokType::Var_v) { consume(); return v; }
        if (cur().type == TokType::Var_r) { consume(); return r; }
        if (cur().type == TokType::Var_c) { consume(); return c; }
        if (cur().type == TokType::Func) {
            QString fn = consume().name;
            if (cur().type != TokType::LParen) { ok = false; return 0; }
            consume(); // (
            double a = parseExpr();
            if (fn == "abs") { if (cur().type != TokType::RParen) ok = false; else consume(); return qAbs(a); }
            if (fn == "sqrt") { if (cur().type != TokType::RParen) ok = false; else consume(); return a < 0 ? (ok=false,0.0) : std::sqrt(a); }
            if (fn == "floor") { if (cur().type != TokType::RParen) ok = false; else consume(); return std::floor(a); }
            if (fn == "ceil") { if (cur().type != TokType::RParen) ok = false; else consume(); return std::ceil(a); }
            if (fn == "round") { if (cur().type != TokType::RParen) ok = false; else consume(); return std::round(a); }
            // Two-arg
            if (cur().type != TokType::Comma) { ok = false; return 0; } consume();
            double b = parseExpr();
            if (cur().type != TokType::RParen) { ok = false; return 0; } consume();
            if (fn == "pow") return std::pow(a, b);
            if (fn == "min") return qMin(a, b);
            if (fn == "max") return qMax(a, b);
            ok = false; return 0;
        }
        if (cur().type == TokType::LParen) {
            consume();
            double val = parseExpr();
            if (cur().type != TokType::RParen) { ok = false; return 0; }
            consume();
            return val;
        }
        ok = false;
        return 0;
    }
};

static double evalExpr(const QVector<Token> &toks, double v, double r, double c, bool &ok)
{
    ExprParser p{toks, 0, v, r, c, true};
    double result = p.parseExpr();
    ok = p.ok && p.cur().type == TokType::End;
    return result;
}

} // anonymous namespace

QString AIToolExecutor::toolEvaluateMapExpression(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    const QString name = input["map_name"].toString();
    MapInfo *m = findMap(name);
    if (!m) return mapNotFound(name);

    {
        const int dx = m->dimensions.x, dy = m->dimensions.y;
        if (dx <= 0 || dy <= 0 || dx > 100000 || dy > 100000 ||
            qint64(dx) * dy * m->dataSize > qint64(targetDataConst().size())) {
            QJsonObject err; err["error"] = "Map dimensions out of range";
            return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
        }
    }

    const QString expr = input["expression"].toString().trimmed();
    if (expr.isEmpty()) {
        QJsonObject err; err["error"] = "expression is empty";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Tokenize
    bool tokOk = true;
    QVector<Token> toks = tokenize(expr, tokOk);
    if (!tokOk) {
        QJsonObject err; err["error"] = "Invalid expression syntax: " + expr;
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const auto *rawData = reinterpret_cast<const uint8_t *>(targetDataConst().constData());
    int dataLen = targetDataConst().size();
    uint32_t base = mapOffsetInTarget(*m);
    int cols = m->dimensions.x, rows = m->dimensions.y;
    if (cols <= 0 || rows <= 0) {
        QJsonObject err; err["error"] = "Map has invalid dimensions";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Compute new values and build preview
    struct NewVal { int r, c; double oldPhys, newPhys; };
    QVector<NewVal> newVals;
    newVals.reserve(cols * rows);

    for (int ri = 0; ri < rows; ++ri) {
        for (int ci = 0; ci < cols; ++ci) {
            int idx = m->columnMajor ? (ci * rows + ri) : (ri * cols + ci);
            uint32_t off = base + (uint32_t)(idx * m->dataSize);
            uint32_t raw = readRomValue(rawData, dataLen, off, m->dataSize, m_project->byteOrder);
            double phys = m->scaling.toPhysical(signExtendRaw(raw, m->dataSize, m->dataSigned));

            bool exprOk = true;
            double newPhys = evalExpr(toks, phys, (double)ri, (double)ci, exprOk);
            if (!exprOk) {
                QJsonObject err;
                err["error"] = QString("Expression evaluation failed at row %1, col %2").arg(ri).arg(ci);
                return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
            }
            newVals.append({ri, ci, phys, newPhys});
        }
    }

    // Preview: show a few changed cells
    QString preview;
    int shown = 0;
    for (const auto &nv : newVals) {
        if (qAbs(nv.newPhys - nv.oldPhys) > 1e-9 && shown < 6) {
            preview += QString("  [%1,%2] %3 → %4 %5\n")
                .arg(nv.r).arg(nv.c)
                .arg(nv.oldPhys, 0, 'f', 3)
                .arg(nv.newPhys, 0, 'f', 3)
                .arg(m->scaling.unit);
            ++shown;
        }
    }
    int changedCells = 0;
    for (const auto &nv : newVals) if (qAbs(nv.newPhys - nv.oldPhys) > 1e-9) ++changedCells;
    if (changedCells > shown) preview += QString("  ... and %1 more cells\n").arg(changedCells - shown);

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(),
        tr("AI: Apply Expression"),
        tr("Apply expression '%1' to map '%2'?\n\n%3 cells will change (%4 total).\n\nPreview:\n%5\nApply?")
            .arg(expr, name).arg(changedCells).arg(cols * rows).arg(preview),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot("expression: " + expr + " on " + name);

    // Apply
    auto *writableData = reinterpret_cast<uint8_t *>(targetData().data());
    for (const auto &nv : newVals) {
        int idx = m->columnMajor ? (nv.c * rows + nv.r) : (nv.r * cols + nv.c);
        uint32_t off = base + (uint32_t)(idx * m->dataSize);
        double rawD = m->scaling.toRaw(nv.newPhys);
        if (m->dataSigned) {
            double minS = (m->dataSize == 1) ? -128.0 : (m->dataSize == 4) ? -2147483648.0 : -32768.0;
            double maxS = (m->dataSize == 1) ?  127.0 : (m->dataSize == 4) ?  2147483647.0 :  32767.0;
            rawD = qBound(minS, rawD, maxS);
        } else {
            double maxU = (m->dataSize == 1) ? 255.0 : (m->dataSize == 4) ? 4294967295.0 : 65535.0;
            rawD = qBound(0.0, rawD, maxU);
        }
        writeRomValue(writableData, dataLen, off, m->dataSize, m_project->byteOrder, (uint32_t)(int32_t)std::round(rawD));
    }
    m_project->modified = true;
    emit projectModified();

    QJsonObject res;
    res["success"]       = true;
    res["map"]           = name;
    res["expression"]    = expr;
    res["total_cells"]   = cols * rows;
    res["changed_cells"] = changedCells;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── apply_delta_to_rom ────────────────────────────────────────────────────────

QString AIToolExecutor::toolApplyDeltaToRom(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    const int srcIdx = input["source_index"].toInt(-1);
    if (srcIdx < 0 || srcIdx >= m_project->linkedRoms.size()) {
        QJsonObject err;
        err["error"] = QString("Invalid source_index %1. Use list_linked_roms.").arg(srcIdx);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const LinkedRom &src = m_project->linkedRoms[srcIdx];
    if (src.data.isEmpty()) {
        QJsonObject err; err["error"] = "Linked ROM has no data";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    // Determine which maps to copy. `map_names` is retained for compatibility,
    // but must resolve uniquely; `map_refs` supplies the stable address needed
    // for duplicate names.
    QSet<MapInfo*> selectedMaps;
    bool hasExplicitFilter = false;
    QJsonArray namesArr = input["map_names"].toArray();
    for (const auto &v : namesArr) {
        const QString selector = v.toString();
        if (selector.isEmpty()) continue;
        hasExplicitFilter = true;
        MapInfo *m = findMap(selector);
        if (!m) return mapNotFound(selector);
        selectedMaps.insert(m);
    }
    for (const QJsonValue &value : input["map_refs"].toArray()) {
        const QJsonObject ref = value.toObject();
        QString selector = ref.value("address").toString().trimmed();
        if (!selector.isEmpty()) selector.prepend('@');
        else selector = ref.value("name").toString();
        if (selector.isEmpty()) {
            QJsonObject err; err["error"] = "Each map_refs item needs a name or address";
            return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
        }
        hasExplicitFilter = true;
        MapInfo *m = findMap(selector);
        if (!m) return mapNotFound(selector);
        selectedMaps.insert(m);
    }

    const auto *srcData = reinterpret_cast<const uint8_t *>(src.data.constData());
    int srcLen = src.data.size();

    struct DeltaMap { MapInfo *m; uint32_t srcOff; };
    QVector<DeltaMap> toCopy;

    for (MapInfo &m : m_project->maps) {
        if (hasExplicitFilter && !selectedMaps.contains(&m)) continue;
        if (!src.mapOffsets.contains(m.name)) continue;
        if (src.mapConfidence.value(m.name, 0) < 60) continue;

        uint32_t srcOff = src.mapOffsets[m.name] + m.mapDataOffset;
        int len = m.dimensions.x * m.dimensions.y * m.dataSize;
        if (len <= 0 || (int)srcOff + len > srcLen) continue;

        // Check if source actually differs from main ROM original
        const auto *origData = reinterpret_cast<const uint8_t *>(m_project->originalData.constData());
        uint32_t mainOff = m.address + m.mapDataOffset;
        if ((int)mainOff + len > m_project->originalData.size()) continue;
        if (memcmp(srcData + srcOff, origData + mainOff, len) == 0) continue; // no change

        toCopy.append({&m, srcOff});
    }

    if (toCopy.isEmpty()) {
        QJsonObject res;
        res["success"] = true;
        res["copied"]  = 0;
        res["message"] = "No modified maps found in linked ROM to copy.";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    QString preview;
    for (const auto &dm : toCopy)
        preview += QString("  • %1\n").arg(dm.m->name);

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(),
        tr("AI: Apply Delta from Linked ROM"),
        tr("Apply modifications from '%1' to current ROM?\n\n%2 maps will be updated:\n%3\nApply?")
            .arg(src.label).arg(toCopy.size()).arg(preview),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    ensureVersionSnapshot("apply delta from: " + src.label);

    auto *destData = reinterpret_cast<uint8_t *>(targetData().data());
    int destLen = targetData().size();
    int copied = 0;

    for (const auto &dm : toCopy) {
        uint32_t dstOff = mapOffsetInTarget(*dm.m);
        int len = dm.m->dimensions.x * dm.m->dimensions.y * dm.m->dataSize;
        if ((int)dstOff + len > destLen) continue;
        memcpy(destData + dstOff, srcData + dm.srcOff, len);
        ++copied;
    }

    m_project->modified = true;
    emit projectModified();

    QJsonArray copiedNames;
    QJsonArray copiedRefs;
    for (const auto &dm : toCopy) {
        copiedNames.append(dm.m->name);
        QJsonObject ref{{"name", dm.m->name}};
        addMapIdentity(ref, *dm.m);
        copiedRefs.append(ref);
    }
    QJsonObject res;
    res["success"] = true;
    res["source"]  = src.label;
    res["copied"]  = copied;
    res["maps"]    = copiedNames;
    res["mapRefs"] = copiedRefs;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── append_tuning_note ────────────────────────────────────────────────────────

QString AIToolExecutor::toolAppendTuningNote(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    const QString message = input["message"].toString().trimmed();
    if (message.isEmpty()) {
        QJsonObject err; err["error"] = "message is empty";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    TuningLogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.author    = "AI";
    entry.mapName   = input["map_name"].toString();
    entry.category  = input["category"].toString("note");
    entry.message   = message;

    m_project->tuningLog.append(entry);

    QJsonObject res;
    res["success"]   = true;
    res["entry"]     = m_project->tuningLog.size() - 1;
    res["timestamp"] = entry.timestamp.toString(Qt::ISODate);
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── log_dyno_result ───────────────────────────────────────────────────────────

QString AIToolExecutor::toolLogDynoResult(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    if (!input.contains("peak_power")) {
        QJsonObject err; err["error"] = "peak_power is required";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    DynoResult run;
    run.timestamp  = QDateTime::currentDateTime();
    run.peakPower  = input["peak_power"].toDouble();
    run.powerUnit  = input["power_unit"].toString("PS");
    run.peakTorque = input["peak_torque"].toDouble();
    run.rpmAtPower = input["rpm_at_power"].toInt();
    run.notes      = input["notes"].toString();

    // Record current modification summary as context
    QJsonArray mods;
    const auto *orig = reinterpret_cast<const uint8_t *>(m_project->originalData.constData());
    const auto *curr = reinterpret_cast<const uint8_t *>(m_project->currentData.constData());
    int sz = qMin(m_project->originalData.size(), m_project->currentData.size());
    for (const MapInfo &m : m_project->maps) {
        int len = m.dimensions.x * m.dimensions.y * m.dataSize;
        if (len <= 0 || (int)m.address + len > sz) continue;
        if (memcmp(orig + m.address, curr + m.address, len) != 0)
            mods.append(m.name);
    }
    run.modifications = QString::fromUtf8(QJsonDocument(mods).toJson(QJsonDocument::Compact));

    m_project->dynoLog.append(run);

    // Build comparison with previous run if any
    QString comparison;
    if (m_project->dynoLog.size() >= 2) {
        const DynoResult &prev = m_project->dynoLog[m_project->dynoLog.size() - 2];
        double pwrDelta = run.peakPower - prev.peakPower;
        double tqDelta  = run.peakTorque - prev.peakTorque;
        comparison = QString("vs previous run: %1%2 %3, %4%5 Nm")
            .arg(pwrDelta >= 0 ? "+" : "").arg(pwrDelta, 0, 'f', 1).arg(run.powerUnit)
            .arg(tqDelta >= 0 ? "+" : "").arg(tqDelta, 0, 'f', 1);
    }

    QJsonObject res;
    res["success"]       = true;
    res["run_number"]    = m_project->dynoLog.size();
    res["peak_power"]    = run.peakPower;
    res["power_unit"]    = run.powerUnit;
    res["peak_torque"]   = run.peakTorque;
    res["rpm"]           = run.rpmAtPower;
    res["mods_recorded"] = mods.size();
    if (!comparison.isEmpty()) res["comparison"] = comparison;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

// ── undo_with_reason ──────────────────────────────────────────────────────────

QString AIToolExecutor::toolUndoWithReason(const QJsonObject &input)
{
    if (!m_project) return noProjectError();

    const int vIdx  = input["version_index"].toInt(-1);
    const QString reason = input["reason"].toString().trimmed();

    if (vIdx < 0 || vIdx >= m_project->versions.size()) {
        QJsonObject err;
        err["error"] = QString("Invalid version_index %1. Use list_versions.").arg(vIdx);
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }
    if (reason.isEmpty()) {
        QJsonObject err; err["error"] = "reason is required";
        return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
    }

    const ProjectVersion &v = m_project->versions[vIdx];

    QMessageBox::StandardButton btn = QMessageBox::question(
        qApp->activeWindow(),
        tr("AI: Rollback ROM"),
        tr("Restore ROM to version:\n\"%1\" (%2)\n\nReason: %3\n\nProceed?")
            .arg(v.name, v.created.toString("dd.MM.yyyy HH:mm:ss"), reason),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        QJsonObject res; res["error"] = "cancelled";
        return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
    }

    // Log the rollback
    TuningLogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.author    = "AI";
    entry.category  = "modification";
    entry.message   = QString("Rolled back to version '%1': %2").arg(v.name, reason);
    m_project->tuningLog.append(entry);

    m_project->restoreVersion(vIdx);
    emit projectModified();

    QJsonObject res;
    res["success"]    = true;
    res["restored_to"]= v.name;
    res["reason"]     = reason;
    res["logged"]     = true;
    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}
