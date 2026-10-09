param(
    [string]$Server='E:\Scratch\enshrouded-flight-creative-20261008\enshrouded_server.exe',
    [string]$Client='D:\Games\SteamLibrary\steamapps\common\Enshrouded\enshrouded.exe'
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$header=Get-Content -LiteralPath (Join-Path $root 'src\creative_flight_call_proof.hpp') -Raw
$inputs=@(
    @('server',$Server,'001C1B40ED091D8C1AEE583ADDE3800D7C858AE2C7F4DFF54FCA2938B2BE1637',35),
    @('client',$Client,'AF2F5A1227911D8AA06B3908D6BD0211838211CAE14EA91099CB57D0DF990781',23)
)
foreach($inputImage in $inputs){
    if((Get-FileHash -LiteralPath $inputImage[1] -Algorithm SHA256).Hash -ne $inputImage[2]){throw 'Pinned image hash mismatch'}
    $data=[IO.File]::ReadAllBytes($inputImage[1])
    $nt=[BitConverter]::ToInt32($data,0x3c)
    $count=[BitConverter]::ToUInt16($data,$nt+6)
    $sections=$nt+24+[BitConverter]::ToUInt16($data,$nt+20)
    $block=[regex]::Match($header,($inputImage[0]+'_proof\[\]\s*=\s*\{(.*?)\n\};'),[Text.RegularExpressions.RegexOptions]::Singleline).Groups[1].Value
    $rows=[regex]::Matches($block,'\{(0x[0-9a-f]+),(\d+),\{([^}]*)\}\}')
    if($rows.Count -ne $inputImage[3]){throw 'Frozen proof row count mismatch'}
    foreach($row in $rows){
        $rva=[Convert]::ToUInt32($row.Groups[1].Value.Substring(2),16)
        $size=[int]$row.Groups[2].Value
        $expected=@([regex]::Matches($row.Groups[3].Value,'0x([0-9a-f]+)') | ForEach-Object{[Convert]::ToByte($_.Groups[1].Value,16)})
        if($expected.Count -ne $size -or $size -lt 1 -or $size -gt 96){throw 'Proof length mismatch'}
        $offset=-1
        for($index=0;$index -lt $count;$index++){
            $s=$sections+40*$index
            $start=[BitConverter]::ToUInt32($data,$s+12)
            $length=[BitConverter]::ToUInt32($data,$s+16)
            if($rva -ge $start -and $rva+$size -le $start+$length){$offset=[BitConverter]::ToUInt32($data,$s+20)+$rva-$start;break}
        }
        if($offset -lt 0){throw 'Proof RVA absent'}
        for($byte=0;$byte -lt $size;$byte++){if($data[$offset+$byte] -ne $expected[$byte]){throw 'Frozen proof byte mismatch'}}
    }
    [pscustomobject]@{Image=$inputImage[0];ProofRows=$rows.Count;PinnedSHA256=$inputImage[2];ReadOnlyProof='PASS'} | ConvertTo-Json -Compress
}
