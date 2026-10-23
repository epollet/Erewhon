# Verifie que le poste est pret pour Erewhon et installe ce qui manque.
# Lance via setup.bat a la racine (voir "setup.bat help").
param(
    [switch]$Check,   # verifie seulement, ne corrige rien
    [switch]$Yes,     # corrige sans demander confirmation
    [switch]$Assets,  # verifie aussi les outils du pipeline d'assets (Python, ffmpeg, .env, Blender)
    [switch]$NoBuild  # ne compile pas le module C++
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'Erewhon.uproject'
$script:Problems = 0

function Write-Section($title) { Write-Host ''; Write-Host "== $title ==" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "  [OK]   $msg" -ForegroundColor Green }
function Write-Info($msg) { Write-Host "         $msg" -ForegroundColor DarkGray }
function Write-Bad($msg)  { Write-Host "  [KO]   $msg" -ForegroundColor Yellow; $script:Problems++ }
function Write-Fixed($msg){ Write-Host "  [FIX]  $msg" -ForegroundColor Green; $script:Problems-- }

# Demande l'autorisation avant une correction ; -Check refuse tout, -Yes accepte tout.
function Confirm-Fix($question) {
    if ($Check) { return $false }
    if ($Yes) { return $true }
    $answer = Read-Host "         $question [o/N]"
    return $answer -match '^(o|oui|y|yes)$'
}

function Test-Command($name) { return [bool](Get-Command $name -ErrorAction SilentlyContinue) }

function Invoke-Git {
    & git -C $Root -c core.quotepath=off @args
    if ($LASTEXITCODE -ne 0) { throw "git $args a echoue (code $LASTEXITCODE)" }
}

# --- Git et Git LFS ----------------------------------------------------------

# Fichiers LFS presents sur le disque mais restes a l'etat de pointeur texte ("oid - chemin").
function Get-LfsPointers {
    Invoke-Git lfs ls-files | Where-Object { $_ -match '^\S+ - (.+)$' } |
        ForEach-Object { $Matches[1] } | Where-Object { Test-Path -LiteralPath (Join-Path $Root $_) }
}

function Test-GitRepo {
    Write-Section 'Git / Git LFS'

    if (-not (Test-Command git)) {
        Write-Bad 'git introuvable dans le PATH'
        if ((Test-Command winget) -and (Confirm-Fix 'Installer Git via winget ?')) {
            winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements
            Write-Info 'Relancez setup.bat dans un nouveau terminal pour prendre en compte le PATH.'
        }
        return
    }
    Write-Ok 'git'

    & git lfs version *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Bad 'git-lfs non installe (les assets .uasset/.umap sont stockes en LFS)'
        if ((Test-Command winget) -and (Confirm-Fix 'Installer Git LFS via winget ?')) {
            winget install --id GitHub.GitLFS -e --accept-source-agreements --accept-package-agreements
            Write-Info 'Relancez setup.bat dans un nouveau terminal pour prendre en compte le PATH.'
        }
        return
    }
    Write-Ok 'git-lfs'

    $gitDir = Join-Path $Root '.git'
    if (-not (Test-Path (Join-Path $gitDir 'hooks\post-checkout'))) {
        Write-Bad 'hooks Git LFS absents du depot'
        if (Confirm-Fix 'Installer les hooks LFS (git lfs install --local) ?') {
            Invoke-Git lfs install --local | Out-Null
            Write-Fixed 'hooks LFS installes'
        }
    }

    # Les assets sont "lockable" (.gitattributes) : sans verrou, LFS les laisse en lecture seule
    # et les scripts d'import de l'editeur ne peuvent plus les reenregistrer.
    $readOnly = & git -C $Root config --get lfs.setlockablereadonly
    if ($readOnly -ne 'false') {
        Write-Bad 'assets LFS en lecture seule (lfs.setlockablereadonly)'
        if (Confirm-Fix 'Laisser les assets modifiables (git config lfs.setlockablereadonly false) ?') {
            Invoke-Git config lfs.setlockablereadonly false
            Write-Fixed 'assets modifiables'
        }
    }

    # Un clone interrompu peut laisser le depot sans index : tout apparait alors "supprime".
    if (-not (Test-Path (Join-Path $gitDir 'index'))) {
        Write-Bad 'index Git absent (clone interrompu ?)'
        if (Confirm-Fix "Reconstruire l'index depuis HEAD (git reset, ne touche pas aux fichiers) ?") {
            Invoke-Git reset -q
            Write-Fixed 'index reconstruit'
        } else { return }
    }

    $missing = @(Invoke-Git ls-files --deleted)
    $pointers = @(Get-LfsPointers)
    if ($missing.Count -eq 0 -and $pointers.Count -eq 0) {
        Write-Ok 'tous les fichiers suivis sont presents et les assets LFS telecharges'
        return
    }
    if ($missing.Count -gt 0) {
        Write-Bad "$($missing.Count) fichier(s) suivi(s) absent(s) du disque, ex. $($missing[0])"
    }
    if ($pointers.Count -gt 0) {
        Write-Bad "$($pointers.Count) asset(s) LFS non telecharge(s) (simples pointeurs)"
    }
    if (-not (Confirm-Fix 'Telecharger les assets LFS et restaurer les fichiers manquants ?')) { return }

    Write-Info 'Telechargement LFS (peut prendre plusieurs minutes)...'
    Invoke-Git lfs fetch
    if ($missing.Count -gt 0) {
        # Restaure uniquement les fichiers absents : les modifications locales ne sont jamais ecrasees.
        # Passe par cmd pour garder la liste -z intacte (chemins accentues).
        & cmd /c "git -C `"$Root`" ls-files -z --deleted | git -C `"$Root`" checkout-index -z --stdin"
        if ($LASTEXITCODE -ne 0) { throw 'restauration des fichiers manquants echouee' }
    }
    Invoke-Git lfs checkout
    $left = @(Invoke-Git ls-files --deleted).Count + @(Get-LfsPointers).Count
    if ($left -eq 0) {
        $script:Problems = [Math]::Max(0, $script:Problems - [int]($missing.Count -gt 0) - [int]($pointers.Count -gt 0))
        Write-Host '  [FIX]  fichiers restaures' -ForegroundColor Green
    } else {
        Write-Bad "$left fichier(s) toujours manquant(s) : verifiez l'acces au remote (git lfs pull)"
    }
}

# --- Unreal Engine -----------------------------------------------------------

function Find-Engine {
    $version = (Get-Content $Project -Raw | ConvertFrom-Json).EngineAssociation
    $candidates = @()
    if ($env:UE_ROOT) { $candidates += $env:UE_ROOT }
    $reg = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$version"
    $installed = (Get-ItemProperty $reg -Name InstalledDirectory -ErrorAction SilentlyContinue).InstalledDirectory
    if ($installed) { $candidates += $installed }
    $candidates += "C:\Program Files\Epic Games\UE_$version"
    foreach ($dir in $candidates) {
        if (Test-Path (Join-Path $dir 'Engine\Binaries\Win64\UnrealEditor.exe')) { return $dir }
    }
    return $null
}

function Test-Engine {
    Write-Section 'Unreal Engine'
    $script:Engine = Find-Engine
    if ($script:Engine) {
        Write-Ok "moteur trouve : $script:Engine"
        if (-not $env:UE_ROOT -and $script:Engine -ne 'C:\Program Files\Epic Games\UE_5.8') {
            Write-Info "run.bat utilise le chemin par defaut ; definissez UE_ROOT=$script:Engine"
        }
    } else {
        Write-Bad "Unreal Engine $((Get-Content $Project -Raw | ConvertFrom-Json).EngineAssociation) introuvable"
        Write-Info "Installez-le depuis l'Epic Games Launcher, ou definissez UE_ROOT."
        return
    }

    # Le moteur refuse un runtime VC++ plus ancien que celui qu'il embarque (souvent celui de VS 2022).
    $redist = Join-Path $script:Engine 'Engine\Extras\Redist\en-us\vc_redist.x64.exe'
    if (-not (Test-Path $redist)) { return }
    $required = [version](Get-Item $redist).VersionInfo.ProductVersion
    $current = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64' -ErrorAction SilentlyContinue).Version
    if ($current -and [version]$current.TrimStart('v') -ge $required) {
        Write-Ok "runtime Visual C++ $($current.TrimStart('v'))"
        return
    }
    Write-Bad "runtime Visual C++ $current trop ancien (requis : $required)"
    if (Confirm-Fix 'Installer le runtime fourni avec le moteur (droits administrateur) ?') {
        $proc = Start-Process $redist -ArgumentList '/install', '/passive', '/norestart' -Verb RunAs -Wait -PassThru
        # 3010 : installe, redemarrage recommande.
        if ($proc.ExitCode -in 0, 3010) { Write-Fixed 'runtime Visual C++ installe' }
        else { Write-Host "  [KO]   installation du runtime echouee (code $($proc.ExitCode))" -ForegroundColor Yellow }
    }
}

# --- Visual Studio (compilation C++) -----------------------------------------

function Test-VisualStudio {
    Write-Section 'Visual Studio / MSVC'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = $null
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    $sdk = Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include\10.0.22621.0"
    if ($vsPath -and $sdk) {
        Write-Ok "MSVC : $vsPath"
        Write-Ok 'Windows SDK 10.0.22621'
        return
    }

    $vsConfig = Join-Path $Root '.vsconfig'
    $anyVs = $null
    if (Test-Path $vswhere) { $anyVs = & $vswhere -latest -products * -property installationPath }

    if (-not $anyVs) {
        Write-Bad "Visual Studio absent (requis pour compiler le module C++)"
        if (-not (Test-Command winget)) { Write-Info 'Installez VS 2022 Community avec le fichier .vsconfig du projet.'; return }
        if (Confirm-Fix 'Installer Visual Studio 2022 Community avec les composants de .vsconfig (~20 Go) ?') {
            winget install --id Microsoft.VisualStudio.2022.Community -e --accept-source-agreements --accept-package-agreements `
                --override "--passive --wait --norestart --config `"$vsConfig`""
            if ($LASTEXITCODE -eq 0) { Write-Fixed 'Visual Studio installe' } else { Write-Bad "installation VS echouee (code $LASTEXITCODE)" }
        }
    } else {
        if (-not $vsPath) { Write-Bad "Visual Studio present ($anyVs) mais sans les outils C++ MSVC" }
        if (-not $sdk) { Write-Bad 'Windows SDK 10.0.22621 absent' }
        $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
        if (Confirm-Fix 'Ajouter les composants de .vsconfig a cette installation ?') {
            $proc = Start-Process $installer -ArgumentList @('modify', '--installPath', "`"$anyVs`"", '--config', "`"$vsConfig`"", '--passive', '--norestart') -Wait -PassThru -Verb RunAs
            if ($proc.ExitCode -eq 0) { Write-Fixed 'composants installes' } else { Write-Bad "modification VS echouee (code $($proc.ExitCode))" }
        }
    }
}

# --- Compilation du module Erewhon -------------------------------------------

function Test-Build {
    Write-Section 'Module C++ Erewhon'
    $dll = Join-Path $Root 'Binaries\Win64\UnrealEditor-Erewhon.dll'
    $newest = Get-ChildItem (Join-Path $Root 'Source') -Recurse -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ((Test-Path $dll) -and (Get-Item $dll).LastWriteTime -ge $newest.LastWriteTime) {
        Write-Ok 'module compile et a jour'
        return
    }
    if (Test-Path $dll) { Write-Bad "module plus ancien que les sources ($($newest.Name))" }
    else { Write-Bad 'module non compile (Binaries\Win64\UnrealEditor-Erewhon.dll absent) : run.bat ne peut pas demarrer' }

    if ($NoBuild -or -not $script:Engine) { return }
    if (-not (Confirm-Fix 'Compiler ErewhonEditor (Development Win64) ?')) { return }
    $build = Join-Path $script:Engine 'Engine\Build\BatchFiles\Build.bat'
    & $build ErewhonEditor Win64 Development "-Project=$Project" -WaitMutex
    if ($LASTEXITCODE -eq 0) { Write-Fixed 'module compile' }
    else { Write-Host "  [KO]   compilation echouee (code $LASTEXITCODE)" -ForegroundColor Yellow }
}

# --- Pipeline d'assets (optionnel) -------------------------------------------

function Find-Python {
    # Le python.exe de WindowsApps n'est qu'un raccourci vers le Microsoft Store.
    foreach ($cmd in @('py', 'python')) {
        $c = Get-Command $cmd -ErrorAction SilentlyContinue
        if (-not $c -or $c.Source -like '*\WindowsApps\*') { continue }
        $extra = @()
        if ($cmd -eq 'py') { $extra = @('-3') }
        & $c.Source @extra --version *> $null
        if ($LASTEXITCODE -eq 0) { return [pscustomobject]@{ Exe = $c.Source; Args = $extra } }
    }
    return $null
}

function Invoke-Python {
    param($py)
    $rest = $args
    & $py.Exe @($py.Args) @rest
}

function Test-AssetTools {
    Write-Section "Pipeline d'assets (Tools/assetgen, Tools/Editor)"

    $py = Find-Python
    if (-not $py) {
        Write-Bad 'Python 3 absent (le python.exe du Store ne compte pas)'
        if ((Test-Command winget) -and (Confirm-Fix 'Installer Python 3.12 via winget ?')) {
            winget install --id Python.Python.3.12 -e --accept-source-agreements --accept-package-agreements
            Write-Info 'Relancez setup.bat -Assets dans un nouveau terminal.'
        }
    } else {
        Write-Ok "Python : $(Invoke-Python $py --version)"
        Invoke-Python $py -c 'import PIL' *> $null
        if ($LASTEXITCODE -eq 0) { Write-Ok 'Pillow (font_atlas.py)' }
        else {
            Write-Bad 'Pillow absent (requis par font_atlas.py)'
            if (Confirm-Fix 'Installer Pillow (pip install --user pillow) ?') {
                Invoke-Python $py -m pip install --user pillow
                if ($LASTEXITCODE -eq 0) { Write-Fixed 'Pillow installe' }
            }
        }
    }

    if (Test-Command ffmpeg) { Write-Ok 'ffmpeg (conversion des musiques a l''import)' }
    else {
        Write-Bad 'ffmpeg absent : import_generated.py ignorera les musiques mp3'
        if ((Test-Command winget) -and (Confirm-Fix 'Installer ffmpeg via winget ?')) {
            winget install --id Gyan.FFmpeg -e --accept-source-agreements --accept-package-agreements
            if ($LASTEXITCODE -eq 0) { Write-Fixed 'ffmpeg installe (nouveau terminal requis)' }
        }
    }

    $envFile = Join-Path $Root '.env'
    $keys = @('GEMINI_API_KEY', 'MESHY_API_KEY', 'ELEVENLAB_API_KEY')
    if (-not (Test-Path $envFile)) {
        Write-Bad '.env absent (cles API Gemini / Meshy / ElevenLabs)'
        if (Confirm-Fix 'Creer un .env vide a remplir ?') {
            Set-Content -Path $envFile -Encoding ascii -Value ($keys | ForEach-Object { "$_=" })
            Write-Fixed ".env cree : renseignez les cles dans $envFile"
        }
    } else {
        $content = Get-Content $envFile
        $empty = $keys | Where-Object { $k = $_; -not ($content | Where-Object { $_ -match "^\s*$k\s*=\s*\S" }) }
        if ($empty) { Write-Bad ".env : cle(s) vide(s) ou absente(s) : $($empty -join ', ')" }
        else { Write-Ok '.env et cles API' }
    }

    if (Test-Path (Join-Path $Root 'Tools\Blender\blender.exe')) { Write-Ok 'Blender portable (Tools\Blender)' }
    else {
        Write-Bad 'Blender portable absent (Tools\Blender\blender.exe, requis par quadruped_rig.py)'
        Write-Info 'Telechargez le zip "portable" sur blender.org et extrayez-le dans Tools\Blender.'
    }
}

# --- Main --------------------------------------------------------------------

Write-Host "Erewhon - verification de l'environnement ($Root)"
if ($Check) { Write-Host 'Mode verification seule : aucune correction ne sera appliquee.' -ForegroundColor DarkGray }

Test-GitRepo
Test-Engine
Test-VisualStudio
Test-Build
if ($Assets) { Test-AssetTools } else { Write-Host ''; Write-Info "Outils du pipeline d'assets non verifies (setup.bat -Assets)." }

Write-Host ''
if ($script:Problems -le 0) {
    Write-Host 'Tout est pret. Lancez run.bat pour jouer.' -ForegroundColor Green
    exit 0
}
Write-Host "$script:Problems probleme(s) restant(s)." -ForegroundColor Yellow
exit 1
