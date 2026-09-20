[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
git -C $repoRoot fetch course --prune
if ($LASTEXITCODE -ne 0) {
    throw '无法从课程发布仓库获取更新。'
}

Write-Host '课程远端分支：'
git -C $repoRoot for-each-ref `
    --sort=refname `
    --format='%(refname:short)  %(objectname:short)  %(committerdate:iso8601)  %(subject)' `
    refs/remotes/course

Write-Host "`nmaster 中的实验课件："
git -C $repoRoot ls-tree -r --name-only course/master -- lab-ppt
