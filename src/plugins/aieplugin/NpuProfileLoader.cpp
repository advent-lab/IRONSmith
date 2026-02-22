// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/NpuProfileLoader.hpp"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Aie {

namespace {

struct ParseContext final {
    Utils::Result result;

    void addError(const QString& message)
    {
        result.addError(message);
    }
};

QString pathKey(const QString& base, const QString& key)
{
    if (base.isEmpty())
        return key;
    return base + QLatin1Char('.') + key;
}

QJsonObject requireObject(const QJsonObject& obj, const QString& key, const QString& path, ParseContext& ctx)
{
    const QJsonValue value = obj.value(key);
    if (!value.isObject()) {
        ctx.addError(QStringLiteral("Expected object at %1").arg(pathKey(path, key)));
        return QJsonObject{};
    }
    return value.toObject();
}

QJsonArray requireArray(const QJsonObject& obj, const QString& key, const QString& path, ParseContext& ctx)
{
    const QJsonValue value = obj.value(key);
    if (!value.isArray()) {
        ctx.addError(QStringLiteral("Expected array at %1").arg(pathKey(path, key)));
        return QJsonArray{};
    }
    return value.toArray();
}

QString requireString(const QJsonObject& obj, const QString& key, const QString& path, ParseContext& ctx)
{
    const QJsonValue value = obj.value(key);
    if (!value.isString()) {
        ctx.addError(QStringLiteral("Expected string at %1").arg(pathKey(path, key)));
        return {};
    }
    return value.toString();
}

int requireInt(const QJsonObject& obj, const QString& key, const QString& path, ParseContext& ctx)
{
    const QJsonValue value = obj.value(key);
    if (!value.isDouble()) {
        ctx.addError(QStringLiteral("Expected integer at %1").arg(pathKey(path, key)));
        return 0;
    }
    return value.toInt();
}

bool readBool(const QJsonObject& obj, const QString& key, bool fallback)
{
    const QJsonValue value = obj.value(key);
    if (!value.isBool())
        return fallback;
    return value.toBool();
}

QVector<int> parseIntArray(const QJsonValue& value, const QString& path, ParseContext& ctx)
{
    QVector<int> out;
    if (!value.isArray()) {
        if (!value.isUndefined() && !value.isNull())
            ctx.addError(QStringLiteral("Expected array at %1").arg(path));
        return out;
    }

    const QJsonArray arr = value.toArray();
    out.reserve(arr.size());
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonValue entry = arr.at(i);
        if (!entry.isDouble()) {
            ctx.addError(QStringLiteral("Expected integer at %1[%2]").arg(path).arg(i));
            continue;
        }
        out.push_back(entry.toInt());
    }
    return out;
}

QStringList parseStringArray(const QJsonValue& value, const QString& path, ParseContext& ctx)
{
    QStringList out;
    if (!value.isArray()) {
        if (!value.isUndefined() && !value.isNull())
            ctx.addError(QStringLiteral("Expected array at %1").arg(path));
        return out;
    }

    const QJsonArray arr = value.toArray();
    out.reserve(arr.size());
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonValue entry = arr.at(i);
        if (!entry.isString()) {
            ctx.addError(QStringLiteral("Expected string at %1[%2]").arg(path).arg(i));
            continue;
        }
        out.push_back(entry.toString());
    }
    return out;
}

TileGroup parseTileGroup(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    TileGroup group;
    group.rows = parseIntArray(obj.value(QStringLiteral("rows")), pathKey(path, QStringLiteral("rows")), ctx);
    group.cols = parseIntArray(obj.value(QStringLiteral("cols")), pathKey(path, QStringLiteral("cols")), ctx);
    group.virtualCols = parseIntArray(obj.value(QStringLiteral("virtualCols")), pathKey(path, QStringLiteral("virtualCols")), ctx);
    return group;
}

GridDefinition parseGridDefinition(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    GridDefinition grid;
    grid.columns = requireInt(obj, QStringLiteral("columns"), path, ctx);

    const QJsonObject rowsObj = requireObject(obj, QStringLiteral("rows"), path, ctx);
    grid.rows.shim = requireInt(rowsObj, QStringLiteral("shim"), pathKey(path, QStringLiteral("rows")), ctx);
    grid.rows.mem = requireInt(rowsObj, QStringLiteral("mem"), pathKey(path, QStringLiteral("rows")), ctx);
    grid.rows.aie = requireInt(rowsObj, QStringLiteral("aie"), pathKey(path, QStringLiteral("rows")), ctx);

    grid.rowOrderBottomToTop = parseStringArray(obj.value(QStringLiteral("rowOrderBottomToTop")),
                                                pathKey(path, QStringLiteral("rowOrderBottomToTop")), ctx);
    return grid;
}

TileLayout parseTileLayout(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    TileLayout layout;
    layout.coordinateSystem = requireString(obj, QStringLiteral("coordinateSystem"), path, ctx);
    layout.shim = parseTileGroup(requireObject(obj, QStringLiteral("shim"), path, ctx), pathKey(path, QStringLiteral("shim")), ctx);
    layout.mem = parseTileGroup(requireObject(obj, QStringLiteral("mem"), path, ctx), pathKey(path, QStringLiteral("mem")), ctx);
    layout.aie = parseTileGroup(requireObject(obj, QStringLiteral("aie"), path, ctx), pathKey(path, QStringLiteral("aie")), ctx);
    return layout;
}

LinuxDeviceMatch parseLinuxDeviceMatch(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    LinuxDeviceMatch linux;
    linux.driver = requireString(obj, QStringLiteral("driver"), path, ctx);
    linux.pciIds = parseStringArray(obj.value(QStringLiteral("pci_ids")), pathKey(path, QStringLiteral("pci_ids")), ctx);
    return linux;
}

DeviceMatch parseDeviceMatch(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    DeviceMatch match;
    const QJsonValue linuxValue = obj.value(QStringLiteral("linux"));
    if (linuxValue.isObject())
        match.linux = parseLinuxDeviceMatch(linuxValue.toObject(), pathKey(path, QStringLiteral("linux")), ctx);
    return match;
}

VirtualShimResolvePolicy parseVirtualShimResolvePolicy(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    VirtualShimResolvePolicy policy;
    policy.strategy = requireString(obj, QStringLiteral("strategy"), path, ctx);
    policy.fallbackOrder = parseIntArray(obj.value(QStringLiteral("fallbackOrder")), pathKey(path, QStringLiteral("fallbackOrder")), ctx);
    return policy;
}

VirtualShimPolicy parseVirtualShimPolicy(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    VirtualShimPolicy policy;
    policy.enabled = readBool(obj, QStringLiteral("enabled"), false);
    policy.virtualShimColumns = parseIntArray(obj.value(QStringLiteral("virtualShimColumns")),
                                             pathKey(path, QStringLiteral("virtualShimColumns")), ctx);

    const QJsonValue resolveValue = obj.value(QStringLiteral("resolveVirtualShimToRealShimColumn"));
    if (resolveValue.isObject()) {
        policy.resolveVirtualShimToRealShimColumn =
            parseVirtualShimResolvePolicy(resolveValue.toObject(), pathKey(path, QStringLiteral("resolveVirtualShimToRealShimColumn")), ctx);
    }
    return policy;
}

NonShimRoutePreference parseNonShimRoutePreference(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    NonShimRoutePreference pref;
    pref.viaColumnsPreference = parseIntArray(obj.value(QStringLiteral("viaColumnsPreference")),
                                              pathKey(path, QStringLiteral("viaColumnsPreference")), ctx);
    return pref;
}

HostInterface parseHostInterface(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    HostInterface iface;
    iface.shimCapableColumns = parseIntArray(obj.value(QStringLiteral("shimCapableColumns")),
                                             pathKey(path, QStringLiteral("shimCapableColumns")), ctx);

    const QJsonObject nonShimObj = obj.value(QStringLiteral("nonShimColumnsRouteVia")).toObject();
    for (auto it = nonShimObj.begin(); it != nonShimObj.end(); ++it) {
        bool ok = false;
        const int column = it.key().toInt(&ok);
        if (!ok) {
            ctx.addError(QStringLiteral("Expected integer key in %1 (got '%2')")
                             .arg(pathKey(path, QStringLiteral("nonShimColumnsRouteVia")), it.key()));
            continue;
        }
        if (!it.value().isObject()) {
            ctx.addError(QStringLiteral("Expected object at %1.%2")
                             .arg(pathKey(path, QStringLiteral("nonShimColumnsRouteVia")), it.key()));
            continue;
        }
        iface.nonShimColumnsRouteVia.insert(column,
                                            parseNonShimRoutePreference(it.value().toObject(),
                                                                        pathKey(pathKey(path, QStringLiteral("nonShimColumnsRouteVia")), it.key()),
                                                                        ctx));
    }

    const QJsonValue virtualShimValue = obj.value(QStringLiteral("virtualShimPolicy"));
    if (virtualShimValue.isObject())
        iface.virtualShimPolicy = parseVirtualShimPolicy(virtualShimValue.toObject(), pathKey(path, QStringLiteral("virtualShimPolicy")), ctx);

    return iface;
}

ColumnSliceHint parseColumnSliceHint(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    ColumnSliceHint hint;
    hint.rows = requireInt(obj, QStringLiteral("rows"), path, ctx);
    hint.rowKindsByIndex = parseStringArray(obj.value(QStringLiteral("rowKindsByIndex")),
                                            pathKey(path, QStringLiteral("rowKindsByIndex")), ctx);
    return hint;
}

IronModelHints parseIronModelHints(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    IronModelHints hints;
    const QJsonValue columnSliceValue = obj.value(QStringLiteral("columnSlice"));
    if (columnSliceValue.isObject())
        hints.columnSlice = parseColumnSliceHint(columnSliceValue.toObject(), pathKey(path, QStringLiteral("columnSlice")), ctx);
    return hints;
}

UnknownDevicePolicy parseUnknownDevicePolicy(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    UnknownDevicePolicy policy;
    const QJsonValue gridValue = obj.value(QStringLiteral("grid"));
    if (gridValue.isObject())
        policy.grid = parseGridDefinition(gridValue.toObject(), pathKey(path, QStringLiteral("grid")), ctx);

    const QJsonValue hostValue = obj.value(QStringLiteral("hostInterface"));
    if (hostValue.isObject())
        policy.hostInterface = parseHostInterface(hostValue.toObject(), pathKey(path, QStringLiteral("hostInterface")), ctx);

    return policy;
}

NpuProfile parseProfile(const QJsonObject& obj, const QString& path, ParseContext& ctx)
{
    NpuProfile profile;
    profile.id = requireString(obj, QStringLiteral("id"), path, ctx);
    profile.name = requireString(obj, QStringLiteral("name"), path, ctx);
    profile.vendor = requireString(obj, QStringLiteral("vendor"), path, ctx);
    profile.family = requireString(obj, QStringLiteral("family"), path, ctx);
    const QJsonValue archValue = obj.value(QStringLiteral("aieArch"));
    if (archValue.isString())
        profile.aieArch = archValue.toString();
    else if (!archValue.isUndefined() && !archValue.isNull())
        ctx.addError(QStringLiteral("Expected string at %1").arg(pathKey(path, QStringLiteral("aieArch"))));

    const QJsonValue matchValue = obj.value(QStringLiteral("match"));
    if (matchValue.isObject())
        profile.match = parseDeviceMatch(matchValue.toObject(), pathKey(path, QStringLiteral("match")), ctx);

    const QJsonValue gridValue = obj.value(QStringLiteral("grid"));
    if (gridValue.isObject())
        profile.grid = parseGridDefinition(gridValue.toObject(), pathKey(path, QStringLiteral("grid")), ctx);
    else
        ctx.addError(QStringLiteral("Missing grid definition at %1").arg(path));

    const QJsonValue tilesValue = obj.value(QStringLiteral("tiles"));
    if (tilesValue.isObject())
        profile.tiles = parseTileLayout(tilesValue.toObject(), pathKey(path, QStringLiteral("tiles")), ctx);
    else
        ctx.addError(QStringLiteral("Missing tiles definition at %1").arg(path));

    const QJsonValue hostValue = obj.value(QStringLiteral("hostInterface"));
    if (hostValue.isObject())
        profile.hostInterface = parseHostInterface(hostValue.toObject(), pathKey(path, QStringLiteral("hostInterface")), ctx);

    const QJsonValue hintsValue = obj.value(QStringLiteral("ironModelHints"));
    if (hintsValue.isObject())
        profile.ironModelHints = parseIronModelHints(hintsValue.toObject(), pathKey(path, QStringLiteral("ironModelHints")), ctx);

    return profile;
}

} // namespace

Utils::Result loadProfileCatalogFromJson(const QByteArray& data, NpuProfileCatalog& out)
{
    ParseContext ctx;

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || doc.isNull()) {
        return Utils::Result::failure(QStringLiteral("Failed to parse NPU profile JSON: %1").arg(error.errorString()));
    }

    if (!doc.isObject()) {
        return Utils::Result::failure(QStringLiteral("NPU profile JSON root is not an object."));
    }

    const QJsonObject root = doc.object();
    out.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(1);

    const QJsonArray devices = requireArray(root, QStringLiteral("devices"), QStringLiteral("root"), ctx);
    out.devices.clear();
    out.devices.reserve(devices.size());

    for (int i = 0; i < devices.size(); ++i) {
        const QJsonValue entry = devices.at(i);
        if (!entry.isObject()) {
            ctx.addError(QStringLiteral("Expected device object at devices[%1]").arg(i));
            continue;
        }
        out.devices.push_back(parseProfile(entry.toObject(), QStringLiteral("devices[%1]").arg(i), ctx));
    }

    const QJsonValue defaultsValue = root.value(QStringLiteral("defaults"));
    if (defaultsValue.isObject()) {
        const QJsonObject defaultsObj = defaultsValue.toObject();
        const QJsonValue unknownPolicyValue = defaultsObj.value(QStringLiteral("unknownDevicePolicy"));
        if (unknownPolicyValue.isObject())
            out.defaults = parseUnknownDevicePolicy(unknownPolicyValue.toObject(), QStringLiteral("defaults.unknownDevicePolicy"), ctx);
    }

    if (!ctx.result)
        return ctx.result;

    return Utils::Result::success();
}

Utils::Result loadProfileCatalogFromFile(const QString& path, NpuProfileCatalog& out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Utils::Result::failure(QStringLiteral("Failed to open NPU profile file: %1").arg(path));

    return loadProfileCatalogFromJson(file.readAll(), out);
}

const NpuProfile* findProfileById(const NpuProfileCatalog& catalog, const QString& id)
{
    for (const auto& profile : catalog.devices) {
        if (profile.id == id)
            return &profile;
    }
    return nullptr;
}

} // namespace Aie