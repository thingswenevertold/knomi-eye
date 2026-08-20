<#
.SYNOPSIS
Dit au boitier si le poste est present ou absent, et installe les
declencheurs Windows qui l'appellent tout seuls.

.DESCRIPTION
Le boitier ne devine plus l'absence a partir d'un minuteur d'inactivite : il
la recoit. Verrouillage de session et mise en veille l'endorment, deverrouillage
et reveil le reveillent. Entre les deux, au repos, il garde les yeux ouverts —
c'est tout l'interet.

Les identifiants ne sont pas dans ce fichier, qui est versionne. Ils vivent
dans tools/presence.local.json, ignore par git :

    {
      "host": "zaza.local",
      "user": "admin",
      "password": "le-mot-de-passe-du-dashboard"
    }

.EXAMPLE
    .\presence-windows.ps1 -Install      # enregistre les 4 declencheurs
    .\presence-windows.ps1 -Away         # essai manuel : le chat s'endort
    .\presence-windows.ps1 -Present      # essai manuel : il se reveille
    .\presence-windows.ps1 -Uninstall
#>
[CmdletBinding(DefaultParameterSetName = 'Send')]
param(
    [Parameter(ParameterSetName = 'Send')][switch]$Away,
    [Parameter(ParameterSetName = 'Send')][switch]$Present,
    [Parameter(ParameterSetName = 'Send')][switch]$Heartbeat,
    [Parameter(ParameterSetName = 'Install')][switch]$Install,
    [Parameter(ParameterSetName = 'Uninstall')][switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$ScriptPath = $MyInvocation.MyCommand.Path
$ConfigPath = Join-Path (Split-Path $ScriptPath) 'presence.local.json'
$TaskPrefix = 'Zaza presence'

function Get-Config {
    if (-not (Test-Path $ConfigPath)) {
        throw "Config absente : $ConfigPath. Voir l'en-tete de ce script pour son contenu."
    }
    $c = Get-Content $ConfigPath -Raw | ConvertFrom-Json
    foreach ($k in 'host', 'user', 'password') {
        if (-not $c.$k) { throw "Champ '$k' manquant dans $ConfigPath" }
    }
    return $c
}

function Send-Presence([bool]$IsAway) {
    $c = Get-Config
    $pair = "$($c.user):$($c.password)"
    $auth = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes($pair))
    $uri = "http://$($c.host)/api/presence?away=$([int]$IsAway)"
    try {
        $r = Invoke-WebRequest -Uri $uri -Headers @{ Authorization = "Basic $auth" } `
                               -TimeoutSec 6 -UseBasicParsing
        Write-Output "$uri -> $($r.StatusCode) $($r.Content)"
    } catch {
        # Un boitier eteint ou hors reseau n'est pas une erreur : le poste se
        # verrouille tout autant. On le signale sans faire echouer la tache,
        # sinon Windows la marquerait en erreur a chaque fois.
        Write-Output "boitier injoignable ($uri) : $($_.Exception.Message)"
    }
}

# Les declencheurs de changement d'etat de session ne sont pas exposes par
# New-ScheduledTaskTrigger, d'ou l'XML. Idem pour les evenements Kernel-Power,
# qui sont des declencheurs sur journal d'evenements.
function New-TaskXml([string]$TriggerXml, [string]$Arg) {
    $exe = (Get-Command powershell.exe).Source
    $args_ = "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$ScriptPath`" $Arg"
    @"
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>Previent Zaza que le poste est $Arg.</Description>
  </RegistrationInfo>
  <Triggers>
$TriggerXml
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>$env:USERNAME</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>LeastPrivilege</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <StartWhenAvailable>false</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <ExecutionTimeLimit>PT1M</ExecutionTimeLimit>
    <Enabled>true</Enabled>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>$exe</Command>
      <Arguments>$args_</Arguments>
    </Exec>
  </Actions>
</Task>
"@
}

function Register-One([string]$Name, [string]$TriggerXml, [string]$Arg) {
    $xml = New-TaskXml $TriggerXml $Arg
    Register-ScheduledTask -TaskName $Name -Xml $xml -Force | Out-Null
    Write-Output "declencheur enregistre : $Name"
}

if ($Heartbeat) {
    # Les quatre declencheurs ne tirent que sur TRANSITION. Or la carte
    # oublie tout a chaque redemarrage — et elle redemarre a chaque envoi
    # OTA — puis retombe sur son minuteur et s'endort au bout de douze
    # minutes alors qu'on est assis devant. Ce battement renvoie l'ETAT
    # toutes les cinq minutes : LogonUI en vie = session verrouillee.
    $locked = [bool](Get-Process -Name LogonUI -ErrorAction SilentlyContinue)
    Send-Presence -IsAway:$locked
    exit 0
}

if ($Install) {
    $sid = "<SessionStateChangeTrigger><StateChange>%STATE%</StateChange><UserId>$env:USERDOMAIN\$env:USERNAME</UserId><Enabled>true</Enabled></SessionStateChangeTrigger>"
    # Kernel-Power 42 = entree en veille, 107 = sortie de veille.
    $evt = @'
    <EventTrigger>
      <Enabled>true</Enabled>
      <Subscription>&lt;QueryList&gt;&lt;Query Id="0" Path="System"&gt;&lt;Select Path="System"&gt;*[System[Provider[@Name='Microsoft-Windows-Kernel-Power'] and EventID=%ID%]]&lt;/Select&gt;&lt;/Query&gt;&lt;/QueryList&gt;</Subscription>
    </EventTrigger>
'@
    Register-One "$TaskPrefix - lock"    ($sid -replace '%STATE%', 'SessionLock')   '-Away'
    Register-One "$TaskPrefix - unlock"  ($sid -replace '%STATE%', 'SessionUnlock') '-Present'
    Register-One "$TaskPrefix - sleep"   ($evt -replace '%ID%', '42')               '-Away'
    Register-One "$TaskPrefix - resume"  ($evt -replace '%ID%', '107')              '-Present'
    # Toutes les cinq minutes, l'etat reel : c'est ce qui repare la carte
    # apres un redemarrage, quand aucune transition ne se produit.
    $beat = @'
    <TimeTrigger>
      <StartBoundary>2026-01-01T00:00:00</StartBoundary>
      <Repetition><Interval>PT5M</Interval></Repetition>
      <Enabled>true</Enabled>
    </TimeTrigger>
'@
    Register-One "$TaskPrefix - heartbeat" $beat '-Heartbeat'
    Write-Output ''
    Write-Output 'Installe. Verrouille ta session (Win+L) pour essayer.'
    exit 0
}

if ($Uninstall) {
    foreach ($n in 'lock', 'unlock', 'sleep', 'resume', 'heartbeat') {
        $full = "$TaskPrefix - $n"
        try { Unregister-ScheduledTask -TaskName $full -Confirm:$false; Write-Output "retire : $full" }
        catch { Write-Output "absent : $full" }
    }
    exit 0
}

if ($Away)    { Send-Presence $true;  exit 0 }
if ($Present) { Send-Presence $false; exit 0 }

Write-Output 'Rien a faire. Utilise -Away, -Present, -Install ou -Uninstall.'
