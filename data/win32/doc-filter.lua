-- doc-filter.lua: pandoc filter for the HTML documents of the Windows
-- package (build-windows-cross.sh).
--
-- Headings get identifiers if the reader gave them none (the man page
-- reader doesn't), so that the table of contents links to them.
--
-- The documents are in doc\, so links to the other converted documents
-- point to their HTML, links to the files beside fuse.exe go one folder
-- up, and links to anything else in the source tree go to GitHub.

local converted = {
  ["README.md"] = "README.html",
  ["README.hu.md"] = "README.hu.html",
  ["CHANGELOG-fork.md"] = "CHANGELOG-fork.html",
  ["CHANGELOG-fork.hu.md"] = "CHANGELOG-fork.hu.html",
}

local in_package = {
  ["README"] = true, ["COPYING"] = true, ["AUTHORS"] = true,
}

local source = "https://github.com/Viktor318/fuse/blob/hungarian-windows/"

function Link( link )
  local target, anchor = link.target:match( "^([^#]*)(#?.*)$" )
  if target == "" or target:match( "^%a[%w+.-]*:" ) then return nil end
  if converted[ target ] then
    link.target = converted[ target ] .. anchor
  elseif in_package[ target ] then
    link.target = "../" .. target .. anchor
  else
    link.target = source .. target .. anchor
  end
  return link
end

local used = {}

function Header( header )
  if header.identifier ~= "" then return nil end
  local id = pandoc.utils.stringify( header.content ):lower()
  id = id:gsub( "[^%w]+", "-" ):gsub( "^-+", "" ):gsub( "-+$", "" )
  if id == "" then id = "section" end
  local base, n = id, 1
  while used[ id ] do
    n = n + 1
    id = base .. "-" .. n
  end
  used[ id ] = true
  header.identifier = id
  return header
end
