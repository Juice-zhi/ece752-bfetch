param([string]$JsonPath, [string]$OutDocx)

$paras = Get-Content -Raw -Encoding UTF8 $JsonPath | ConvertFrom-Json
$word = New-Object -ComObject Word.Application
$word.Visible = $false

function Build($fontName, $fontSize, $lineRule, $lineSpacing, $spaceAfter) {
    $doc = $word.Documents.Add()
    # US Letter with 1in margins: text area runs from 72pt to 720pt
    $doc.PageSetup.PageWidth = 612; $doc.PageSetup.PageHeight = 792
    $doc.PageSetup.TopMargin = 72; $doc.PageSetup.BottomMargin = 72
    $doc.PageSetup.LeftMargin = 72; $doc.PageSetup.RightMargin = 72
    # Chinese Word templates snap lines to a document grid, which Google Docs does not have
    $doc.PageSetup.LayoutMode = 0
    # Body font lives in the Normal style (built-in ids avoid localized style names)
    $normal = $doc.Styles.Item(-1)
    $normal.Font.Name = $fontName; $normal.Font.Size = $fontSize
    $normal.ParagraphFormat.DisableLineHeightGrid = -1
    $normal.ParagraphFormat.LineSpacingRule = $lineRule
    if ($lineRule -eq 5) { $normal.ParagraphFormat.LineSpacing = $lineSpacing }
    $normal.ParagraphFormat.SpaceBefore = 0
    $normal.ParagraphFormat.SpaceAfter = $spaceAfter
    $h = $doc.Styles.Item(-3)   # Heading 2
    $h.Font.Name = $fontName; $h.Font.Size = $fontSize; $h.Font.Bold = 1; $h.Font.Italic = 0
    $h.ParagraphFormat.DisableLineHeightGrid = -1
    $h.Font.Color = -16777216   # automatic
    $h.ParagraphFormat.LineSpacingRule = $lineRule
    if ($lineRule -eq 5) { $h.ParagraphFormat.LineSpacing = $lineSpacing }
    $h.ParagraphFormat.SpaceBefore = 6; $h.ParagraphFormat.SpaceAfter = 2
    $h.ParagraphFormat.KeepWithNext = -1

    $sel = $word.Selection
    $listBlocks = @(); $blockStart = $null; $blockEnd = $null; $absStart = $null; $absEnd = $null
    for ($i = 0; $i -lt $paras.Count; $i++) {
        $p = $paras[$i]
        if ($p.kind -ne 'li' -and $null -ne $blockStart) { $listBlocks += ,@($blockStart, $blockEnd); $blockStart = $null }
        if ($p.kind -eq 'h') {
            $sel.Style = -3   # heading look comes from the style alone
        } else {
            $sel.Style = -1
            $sel.Font.Bold = [int]($p.kind -eq 'title')
            $sel.Font.Size = $(if ($p.kind -eq 'title') { $fontSize + 2 } else { $fontSize })
            $sel.ParagraphFormat.Alignment = $(if ($p.kind -in 'title', 'names') { 1 } else { 0 })
        }
        if ($p.kind -eq 'li' -and $null -eq $blockStart) { $blockStart = $sel.Start }
        if ($p.kind -eq 'p' -and $null -eq $absStart) { $absStart = $sel.Start }
        $sel.TypeText($p.text)
        if ($p.kind -eq 'li') { $blockEnd = $sel.End }
        if ($p.kind -eq 'p') { $absEnd = $sel.End }
        if ($i -lt $paras.Count - 1) { $sel.TypeParagraph() }
    }
    if ($null -ne $blockStart) { $listBlocks += ,@($blockStart, $blockEnd) }
    # Number each run of list items on its own, starting again at 1
    $template = $word.ListGalleries.Item(2).ListTemplates.Item(1)   # wdNumberGallery
    foreach ($b in $listBlocks) { $doc.Range($b[0], $b[1]).ListFormat.ApplyListTemplate($template, $false) }

    $pages = $doc.ComputeStatistics(2)
    $absWords = $doc.Range($absStart, $absEnd).ComputeStatistics(0)
    $end = $doc.Content; $end.Collapse(0)
    $y = [math]::Round($end.Information(6), 1)   # last line, pt from page top
    return @{ doc = $doc; pages = $pages; lastLineY = $y; absWords = $absWords }
}

# Per paragraph: line count and characters on its last line, to see where a short cut saves a line
function LineReport($doc) {
    $sel = $word.Selection
    $i = 0
    foreach ($p in $doc.Paragraphs) {
        $i++
        $r = $p.Range
        $sel.SetRange($r.End - 1, $r.End - 1)
        $null = $sel.HomeKey(5)
        $txt = $r.Text.Substring(0, [math]::Min(40, $r.Text.Length - 1))
        "   {0,2}  lines={1}  lastLineChars={2,3}  {3}" -f $i, $r.ComputeStatistics(1), (($r.End - 1) - $sel.Start), $txt
    }
}

$a = Build 'Times New Roman' 12 0 0 6
"A  TNR 12, single, 6pt after   : pages=$($a.pages)  last line at $($a.lastLineY)pt (text ends 720)  abstract words=$($a.absWords)"
$a.doc.SaveAs2($OutDocx, 16)
$a.doc.Close(0)
$b = Build 'Arial' 11 5 13.8 6
"B  Arial 11, 1.15, 6pt after   : pages=$($b.pages)  last line at $($b.lastLineY)pt"
LineReport $b.doc
$b.doc.Close(0)
$c = Build 'Calibri' 11 5 12.95 8
"C  Calibri 11, 1.08, 8pt after : pages=$($c.pages)  last line at $($c.lastLineY)pt"
$c.doc.Close(0)
$word.Quit()
