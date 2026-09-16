param(
    [string]$RepoRoot = "C:\azerothcore-playerbots"
)

$path = Join-Path $RepoRoot "modules\mod-playerbots\src\Script\Playerbots.cpp"

if (!(Test-Path $path)) {
    throw "Playerbots.cpp not found: $path"
}

$text = Get-Content -Raw -LiteralPath $path

# Group chat: do not feed a normal player's own messages back into their own selfbot AI.
$oldGroup = @'
            PlayerbotAI* const botAI = PlayerbotsMgr::instance().GetPlayerbotAI(member);

            if (botAI == nullptr)
                continue;

            botAI->HandleCommand(type, msg, player);
'@

$newGroup = @'
            PlayerbotAI* const botAI = PlayerbotsMgr::instance().GetPlayerbotAI(member);

            if (botAI == nullptr)
                continue;

            // Restricted selfbot mode: regular players may not issue raw Playerbots
            // commands to their own attached AI through party/raid chat.
            // Server-side wrapper commands call PlayerbotAI::HandleCommand directly
            // and therefore are unaffected.
            if (member == player && !player->CanBeGameMaster())
                continue;

            botAI->HandleCommand(type, msg, player);
'@

if ($text.Contains($newGroup)) {
    Write-Host "Restricted selfbot group-chat lock is already applied."
}
elseif ($text.Contains($oldGroup)) {
    $text = $text.Replace($oldGroup, $newGroup)
    Set-Content -LiteralPath $path -Value $text -NoNewline
    Write-Host "Applied restricted selfbot group-chat lock to mod-playerbots."
}
else {
    throw "Expected Playerbots.cpp group-chat block was not found. mod-playerbots may have changed; patch aborted without modifying the file."
}
