-- SPDX-FileCopyrightText: 2026 The Yantra contributors
-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- pandoc filter for tools/docs/build_docs.sh (DOC-20): relative links to
-- Markdown pages point to the built .html pages. With the metadata
-- "combined" (the one-page manual), links to other chapters
-- ("03-main-window.md#33-x") become in-page anchors ("#33-x"), or the
-- chapter heading's anchor when no fragment is given.
-- Copied from yantra-v1 c24e8f5: tools/docs/pandoc_links.lua.
local combined = false
local chapter_ids = {}

local function slug(s)
  s = pandoc.text.lower(s)
  s = s:gsub("[^%w%s%-_\128-\255]", ""):gsub("%s", "-")
  return s
end

function Meta(m)
  combined = m.combined ~= nil
  if m.chapters then
    for _, c in ipairs(m.chapters) do
      local file, title = pandoc.utils.stringify(c):match("^(%S+)%s+(.+)$")
      if file then chapter_ids[file] = slug(title) end
    end
  end
end

function Link(el)
  local t = el.target
  if t:match("^%a[%w+.-]*:") or t:match("^#") then return nil end
  local path, frag = t:match("^([^#]*)(#?.*)$")
  if path:match("%.md$") then
    local base = path:match("([^/]+)%.md$")
    if combined and chapter_ids[base .. ".md"] and not path:find("/") then
      el.target = (frag ~= "" and frag) or ("#" .. chapter_ids[base .. ".md"])
    else
      el.target = path:gsub("%.md$", ".html") .. frag
    end
  end
  return el
end

return {{Meta = Meta}, {Link = Link}}
