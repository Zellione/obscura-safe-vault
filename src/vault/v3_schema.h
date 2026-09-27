#pragma once

#include <string_view>

namespace vault::v3 {

inline constexpr int SCHEMA_VERSION = 1;

// Phase 105 executable schema contract. Phase 107 wraps it in typed repository
// operations and may only change it through a versioned migration.
inline constexpr std::string_view SCHEMA_SQL = R"sql(
PRAGMA foreign_keys = ON;

CREATE TABLE vault_meta (
    singleton INTEGER PRIMARY KEY CHECK(singleton = 1),
    schema_version INTEGER NOT NULL CHECK(schema_version = 1),
    root_node_id BLOB NOT NULL CHECK(length(root_node_id) = 16),
    app_generation INTEGER NOT NULL DEFAULT 0 CHECK(app_generation >= 0),
    default_sort INTEGER NOT NULL DEFAULT 7 CHECK(default_sort BETWEEN 0 AND 7),
    tiles_show_tags INTEGER NOT NULL DEFAULT 1 CHECK(tiles_show_tags IN (0, 1)),
    migrated_index_version INTEGER NOT NULL DEFAULT 0 CHECK(migrated_index_version BETWEEN 0 AND 13),
    migrated_probe_caps INTEGER NOT NULL DEFAULT 0 CHECK(migrated_probe_caps BETWEEN 0 AND 65535),
    migrated_thumb_side INTEGER NOT NULL DEFAULT 0 CHECK(migrated_thumb_side BETWEEN 0 AND 65535)
);

CREATE TABLE nodes (
    node_id BLOB PRIMARY KEY CHECK(length(node_id) = 16 AND node_id != zeroblob(16)),
    parent_id BLOB REFERENCES nodes(node_id) ON DELETE RESTRICT DEFERRABLE INITIALLY DEFERRED,
    node_type INTEGER NOT NULL CHECK(node_type BETWEEN 0 AND 2),
    display_name TEXT NOT NULL CHECK(length(CAST(display_name AS BLOB)) BETWEEN 1 AND 255),
    sibling_order INTEGER NOT NULL CHECK(sibling_order >= 0),
    favorite INTEGER NOT NULL DEFAULT 0 CHECK(favorite IN (0, 1)),
    created_ts INTEGER NOT NULL DEFAULT 0 CHECK(created_ts >= 0),
    media_format INTEGER,
    width INTEGER CHECK(width IS NULL OR width BETWEEN 0 AND 1000000),
    height INTEGER CHECK(height IS NULL OR height BETWEEN 0 AND 1000000),
    duration_ms INTEGER CHECK(duration_ms IS NULL OR duration_ms >= 0),
    codec INTEGER,
    original_size INTEGER CHECK(original_size IS NULL OR original_size >= 0),
    sort_key INTEGER CHECK(sort_key IS NULL OR sort_key BETWEEN 0 AND 7),
    animated INTEGER CHECK(animated IS NULL OR animated IN (0, 1)),
    UNIQUE(parent_id, display_name COLLATE NOCASE),
    UNIQUE(parent_id, sibling_order)
);
CREATE UNIQUE INDEX one_root_node ON nodes((parent_id IS NULL)) WHERE parent_id IS NULL;
CREATE INDEX nodes_by_parent_order ON nodes(parent_id, sibling_order);

CREATE TABLE objects (
    object_id BLOB PRIMARY KEY CHECK(length(object_id) = 16 AND object_id != zeroblob(16)),
    node_id BLOB NOT NULL REFERENCES nodes(node_id) ON DELETE RESTRICT,
    role INTEGER NOT NULL CHECK(role BETWEEN 1 AND 4),
    encrypted_length INTEGER NOT NULL CHECK(encrypted_length > 0),
    plaintext_length INTEGER NOT NULL CHECK(plaintext_length >= 0),
    frame_plain_limit INTEGER NOT NULL CHECK(frame_plain_limit BETWEEN 1 AND 1048576),
    frame_count INTEGER NOT NULL CHECK(frame_count > 0),
    creation_generation INTEGER NOT NULL CHECK(creation_generation >= 0),
    state INTEGER NOT NULL DEFAULT 1 CHECK(state = 1),
    UNIQUE(node_id, role)
);
CREATE INDEX objects_by_node ON objects(node_id);

CREATE TABLE tags (
    tag_id INTEGER PRIMARY KEY,
    display_name TEXT NOT NULL CHECK(length(CAST(display_name AS BLOB)) BETWEEN 1 AND 255),
    canonical_name TEXT NOT NULL COLLATE NOCASE UNIQUE
);
CREATE TABLE node_tags (
    node_id BLOB NOT NULL REFERENCES nodes(node_id) ON DELETE CASCADE,
    tag_id INTEGER NOT NULL REFERENCES tags(tag_id) ON DELETE CASCADE,
    PRIMARY KEY(node_id, tag_id)
);
CREATE INDEX node_tags_by_tag ON node_tags(tag_id, node_id);

CREATE TABLE tag_categories (
    category_id INTEGER PRIMARY KEY,
    display_name TEXT NOT NULL CHECK(length(CAST(display_name AS BLOB)) BETWEEN 1 AND 64),
    swatch INTEGER NOT NULL CHECK(swatch BETWEEN 0 AND 15),
    UNIQUE(display_name COLLATE NOCASE)
);
CREATE TABLE category_fields (
    category_id INTEGER NOT NULL REFERENCES tag_categories(category_id) ON DELETE CASCADE,
    field_order INTEGER NOT NULL CHECK(field_order >= 0),
    display_name TEXT NOT NULL CHECK(length(CAST(display_name AS BLOB)) BETWEEN 1 AND 64),
    PRIMARY KEY(category_id, field_order),
    UNIQUE(category_id, display_name COLLATE NOCASE)
);
CREATE TABLE tag_field_values (
    tag_id INTEGER NOT NULL REFERENCES tags(tag_id) ON DELETE CASCADE,
    category_id INTEGER NOT NULL,
    field_order INTEGER NOT NULL,
    value TEXT NOT NULL CHECK(length(CAST(value AS BLOB)) <= 256),
    PRIMARY KEY(tag_id, category_id, field_order),
    FOREIGN KEY(category_id, field_order)
        REFERENCES category_fields(category_id, field_order) ON DELETE CASCADE
);
CREATE TABLE tag_descriptions (
    tag_id INTEGER PRIMARY KEY REFERENCES tags(tag_id) ON DELETE CASCADE,
    description TEXT NOT NULL CHECK(length(CAST(description AS BLOB)) <= 512)
);
CREATE TABLE saved_searches (
    search_id INTEGER PRIMARY KEY,
    display_name TEXT NOT NULL CHECK(length(CAST(display_name AS BLOB)) BETWEEN 1 AND 255),
    query BLOB NOT NULL CHECK(length(query) <= 1048576),
    UNIQUE(display_name COLLATE NOCASE)
);

PRAGMA user_version = 1;
)sql";

} // namespace vault::v3
