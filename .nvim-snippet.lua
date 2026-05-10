-- ~/.config/nvim/lua/local/cdeepdive.lua として置くか、
-- init.lua / after/plugin/ などにそのまま貼り付けて使う。
--
-- 動作: 開いている C ソースに対して repo root の ./dx を呼んで Docker 内で
--       make / make run / valgrind / asan / gdb を実行する。
--       開いたバッファは split 下に terminal で出る。

local M = {}

local function repo_root()
  -- 現在のバッファのファイル位置から .git or docker/compose.yml を探して repo root を決める
  local path = vim.fn.expand("%:p:h")
  local found = vim.fn.finddir(".git", path .. ";")
  if found == "" then
    -- fallback: cwd
    return vim.fn.getcwd()
  end
  return vim.fn.fnamemodify(found, ":h")
end

local function dx_run(args)
  local root = repo_root()
  local file_dir = vim.fn.expand("%:p:h")
  -- file_dir を repo_root からの相対 path に
  local rel = file_dir:sub(#root + 2)
  if rel == "" then rel = "." end
  local cmd = string.format("cd %s && ./dx make -C %s %s", root, rel, args or "")
  vim.cmd("split | terminal " .. cmd)
end

function M.make()       dx_run("") end
function M.run()        dx_run("run") end
function M.valgrind()   dx_run("valgrind") end
function M.asan()       dx_run("asan_bug 2>&1 || true") end  -- bug-reproduction でも先に進む
function M.inspect()    dx_run("inspect") end

-- shell へ入る
function M.shell()
  local root = repo_root()
  vim.cmd("split | terminal cd " .. root .. " && ./dx")
end

-- ターミナルバッファのデフォルト挙動: q で閉じる
vim.api.nvim_create_autocmd("TermOpen", {
  callback = function()
    vim.opt_local.number = false
    vim.keymap.set("n", "q", "<cmd>bd!<CR>", { buffer = 0, silent = true })
  end,
})

-- キーマップ (好みで変える)
local map = vim.keymap.set
map("n", "<leader>cm", function() M.make() end,     { desc = "C: dx make" })
map("n", "<leader>cr", function() M.run() end,      { desc = "C: dx run" })
map("n", "<leader>cv", function() M.valgrind() end, { desc = "C: dx valgrind" })
map("n", "<leader>ca", function() M.asan() end,     { desc = "C: dx asan_bug" })
map("n", "<leader>ci", function() M.inspect() end,  { desc = "C: dx inspect" })
map("n", "<leader>cs", function() M.shell() end,    { desc = "C: dx shell" })

return M
