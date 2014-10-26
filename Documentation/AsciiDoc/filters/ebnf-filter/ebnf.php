<?php
/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Vincent Tscherter, tscherter@karmin.ch, Solothurn, 2009-01-18

    2009-01-18 version 0.1 first release
    2009-01-02 version 0.2
      - title und comment literal added
      - ";" als terminator-symbol added

    2014-10-26 slightly modified to optionally read from and write 
               to a file by Hartmut Holzgraefe <hartmut@mariadb.com>

*/

error_reporting(E_ALL|E_STRICT);

//
define('META', 'xis/ebnf v0.2 http://wiki.karmin.ch/ebnf/ gpl3');

// parser
define('EBNF_OPERATOR_TOKEN', 1);
define('EBNF_LITERAL_TOKEN', 2);
define('EBNF_WHITESPACE_TOKEN', 3);
define('EBNF_IDENTIFIER_TOKEN', 4);

// rendering
define('FONT', 3);
define('UNIT', 10);
define('AW', 3);

// lexemes
$ebnf_lexemes[] = array( 'type' => EBNF_OPERATOR_TOKEN, 'expr' => '[={}()|.;[\]]' );
$ebnf_lexemes[] = array( 'type' => EBNF_LITERAL_TOKEN,  'expr' => "\"[^\"]*\"" );
$ebnf_lexemes[] = array( 'type' => EBNF_LITERAL_TOKEN,  'expr' => "'[^']*'" );
$ebnf_lexemes[] = array( 'type' => EBNF_IDENTIFIER_TOKEN,  'expr' => "[a-zA-Z0-9_-]+" );
$ebnf_lexemes[] = array( 'type' => EBNF_WHITESPACE_TOKEN,  'expr' => "\\s+" );

// input example
$input = <<<EOD
"EBNF defined in itself" {
  syntax     = [ title ] "{" { rule } "}" [ comment ].
  rule       = identifier "=" expression ( "." | ";" ) .
  expression = term { "|" term } .
  term       = factor { factor } .
  factor     = identifier
             | literal
             | "[" expression "]"
             | "(" expression ")"
             | "{" expression "}" .
  identifier = character { character } .
  title      = literal .
  comment    = literal .
  literal    = "'" character { character } "'"
             | '"' character { character } '"' .
}
EOD;

$format = "png";

if ($argc == 2) {
   $input_file = $argv[1];

   $output_file = basename($input_file, ".ebnf") . ".$format";   

   $input = file_get_contents($input_file);
}

try {
  $tokens = ebnf_scan($input, true);
  $dom = ebnf_parse_syntax($tokens);
  if ($format == 'xml') {
    header('Content-Type: application/xml');
    echo $dom->saveXML();
  } else {
    render_node($dom->firstChild, true);
  }
} catch (Exception $e) {
    header('Content-Type: text/plain');
    $dom = new DOMDocument();
    $syntax = $dom->createElement("syntax");
    $syntax->setAttribute('title', 'EBNF - Syntax Error');
    $syntax->setAttribute('meta', $e->getMessage());
    $dom->appendChild($syntax);
    render_node($dom->firstChild, true);
}

function rr($im, $x1, $y1, $x2, $y2, $r, $black){
  imageline($im, $x1+$r, $y1, $x2-$r, $y1, $black);
  imageline($im, $x1+$r, $y2, $x2-$r, $y2, $black);
  imageline($im, $x1, $y1+$r, $x1, $y2-$r, $black);
  imageline($im, $x2, $y1+$r, $x2, $y2-$r, $black);
  imagearc($im, $x1+$r, $y1+$r, 2*$r, 2*$r, 180, 270, $black);
  imagearc($im, $x2-$r, $y1+$r, 2*$r, 2*$r, 270, 360, $black);
  imagearc($im, $x1+$r, $y2-$r, 2*$r, 2*$r, 90, 180, $black);
  imagearc($im, $x2-$r, $y2-$r, 2*$r, 2*$r, 0, 90, $black);
}

function create_image($w, $h) {
  global $white, $black, $blue, $red, $green, $silver;
  $im = imagecreatetruecolor($w, $h) or die("no img");
  if (function_exists("imageantialias")) {
    imageantialias($im, true);
  }
  $white = imagecolorallocate ($im, 255, 255, 255);
  $black = imagecolorallocate ($im, 0, 0, 0);
  $blue = imagecolorallocate ($im, 0, 0, 255);
  $red = imagecolorallocate ($im, 255, 0, 0);
  $green = imagecolorallocate ($im, 0, 200, 0);
  $silver = imagecolorallocate ($im, 127, 127, 127);
  imagefilledrectangle($im, 0,0,$w,$h,$white);
  return $im;
}

function arrow($image, $x, $y, $lefttoright) {
  global $white, $black;
  if (!$lefttoright)
    imagefilledpolygon($image,
      array($x, $y-UNIT/3, $x-UNIT, $y, $x, $y+UNIT/3), 3, $black);
  else
    imagefilledpolygon($image,
      array($x-UNIT, $y-UNIT/3, $x, $y, $x-UNIT, $y+UNIT/3), 3, $black);
}


function render_node($node, $lefttoright) {
  global $white, $black, $blue, $red, $green, $silver, $output_file;
  if ($node->nodeName=='identifier' || $node->nodeName=='terminal') {
    $text = $node->getAttribute('value');
    $w = imagefontwidth(FONT)*(strlen($text)) + 4*UNIT;
    $h = 2*UNIT;
    $im = create_image($w, $h);

    if ($node->nodeName!='terminal') {
        imagerectangle($im, UNIT, 0, $w-UNIT-1, $h-1, $black);
      imagestring($im, FONT, 2*UNIT, ($h-imagefontheight(FONT))/2,   $text, $red);
    } else {
      if ($text!="...")
	      rr($im, UNIT, 0, $w-UNIT-1, $h-1, UNIT/2, $black);
      imagestring($im, FONT, 2*UNIT, ($h-imagefontheight(FONT))/2,
        $text, $text!="..."?$blue:$black);
    }
    imageline($im,0,UNIT, UNIT, UNIT, $black);
    imageline($im,$w-UNIT,UNIT, $w+1, UNIT, $black);
    return $im;
  } else if ($node->nodeName=='option' || $node->nodeName=='loop') {
    if ($node->nodeName=='loop')
      $lefttoright = ! $lefttoright;
    $inner = render_node($node->firstChild, $lefttoright);
    $w = imagesx($inner)+6*UNIT;
    $h = imagesy($inner)+2*UNIT;
    $im = create_image($w, $h);
    imagecopy($im, $inner, 3*UNIT, 2*UNIT, 0,0, imagesx($inner), imagesy($inner));
    imageline($im,0,UNIT, $w, UNIT, $black);
    arrow($im, $w/2+UNIT/2, UNIT, $node->nodeName=='loop'?!$lefttoright:$lefttoright);
    arrow($im, 3*UNIT, 3*UNIT, $lefttoright);
    arrow($im, $w-2*UNIT, 3*UNIT, $lefttoright);
    imageline($im,UNIT,UNIT, UNIT, 3*UNIT, $black);
    imageline($im,UNIT,3*UNIT, 2*UNIT, 3*UNIT, $black);
    imageline($im,$w-UNIT,UNIT, $w-UNIT, 3*UNIT, $black);
	imageline($im,$w-3*UNIT-1,3*UNIT, $w-UNIT, 3*UNIT, $black);
    return $im;
  } else if ($node->nodeName=='sequence') {
    $inner = render_childs($node, $lefttoright);
    if (!$lefttoright)
      $inner = array_reverse($inner);
    $w = count($inner)*UNIT-UNIT; $h = 0;
    for ($i = 0; $i<count($inner); $i++) {
      $w += imagesx($inner[$i]);
      $h = max($h, imagesy($inner[$i]));
    } $im = create_image($w, $h);
    imagecopy($im, $inner[0], 0, 0, 0,0, imagesx($inner[0]), imagesy($inner[0]));
    $x = imagesx($inner[0])+UNIT;
    for ($i = 1; $i<count($inner); $i++) {
      imageline($im, $x-UNIT-1, UNIT, $x, UNIT, $black);
      arrow($im, $x, UNIT, $lefttoright);
      imagecopy($im, $inner[$i], $x, 0, 0,0, imagesx($inner[$i]), imagesy($inner[$i]));
      $x += imagesx($inner[$i])+UNIT;
    } return $im;
  } else if ($node->nodeName=='choise') {
    $inner = render_childs($node, $lefttoright);
    $h = (count($inner)-1)*UNIT; $w = 0;
    for ($i = 0; $i<count($inner); $i++) {
      $h += imagesy($inner[$i]);
      $w = max($w, imagesx($inner[$i]));
    } $w += 6*UNIT; $im = create_image($w, $h); $y = 0;
    imageline($im, 0, UNIT, UNIT, UNIT, $black);
    imageline($im, $w-UNIT, UNIT, $w, UNIT, $black);
    for ($i = 0; $i<count($inner); $i++) {
      imageline($im, UNIT, $y+UNIT, $w-UNIT, $y+UNIT, $black);
      imagecopy($im, $inner[$i], 3*UNIT, $y, 0,0, imagesx($inner[$i]), imagesy($inner[$i]));
      arrow($im, 3*UNIT, $y+UNIT, $lefttoright);
      arrow($im, $w-2*UNIT, $y+UNIT, $lefttoright);
      $top = $y + UNIT;
      $y += imagesy($inner[$i])+UNIT;
    }
    imageline($im, UNIT, UNIT, UNIT, $top, $black);
    imageline($im, $w-UNIT, UNIT, $w-UNIT, $top, $black);
    return $im;
  } else if ($node->nodeName=='syntax') {
    $title = $node->getAttribute('title');
    $meta = $node->getAttribute('meta');
    $node = $node->firstChild;
    $names = array();
    $images = array();
    while ($node!=null) {
	   $names[] = $node->getAttribute('name');
	   $im = render_node($node->firstChild, $lefttoright);
	   $images[] = $im;
       $node = $node->nextSibling;
    } $wn  = 0; $wr = 0; $h = 5*UNIT;
    for ($i = 0; $i<count($images); $i++) {
      $wn = max($wn, imagefontwidth(FONT)*strlen($names[$i]));
      $wr = max($wr, imagesx($images[$i]));
	  $h += imagesy($images[$i])+2*UNIT;
    }
    if ($title=='') $h -= 2*UNIT;
    if ($meta=='') $h -= 2*UNIT;
    $w = max($wr+$wn+3*UNIT, imagefontwidth(1)*strlen($meta)+2*UNIT);
    $im = create_image($w, $h);
    $y = 2*UNIT;
    if ($title!='') {
      imagestring($im, FONT, UNIT, (2*UNIT-imagefontheight(FONT))/2,
      $title, $green);
      imageline($im, 0, 2*UNIT, $w, 2*UNIT, $green);
      $y += 2*UNIT;
    }
    for ($i = 0; $i<count($images); $i++) {
      imagestring($im, FONT, UNIT, $y-UNIT+(2*UNIT-imagefontheight(FONT))/2, $names[$i], $red);
      imagecopy($im, $images[$i], $wn+2*UNIT, $y, 0,0, imagesx($images[$i]) , imagesy($images[$i]));
      imageline($im, UNIT, $y+UNIT, $wn+2*UNIT, $y+UNIT, $black);
      imageline($im, $wn+2*UNIT+imagesx($images[$i])-1, $y+UNIT, $w-UNIT, $y+UNIT, $black);
      imageline($im, $w-UNIT, $y+UNIT/2, $w-UNIT ,$y+1.5*UNIT, $black);
      $y += 2*UNIT + imagesy($images[$i]);
    }
    imagestring($im, 1, UNIT, $h-2*UNIT+(2*UNIT-imagefontheight(1))/2,
      $meta, $silver);
    rr($im, 0,0,$w-1, $h-1, UNIT/2, $green);

    if (isset($output_file)) {
      imagepng($im, $output_file);
    } else {
      header('Content-Type: image/png');
      imagepng($im);
    }
    return $im;
  }
}

function render_childs($node, $lefttoright) {
   $childs = array();
   $node = $node->firstChild;
   while ($node!=null) {
     $childs[] = render_node($node, $lefttoright);
     $node = $node->nextSibling;
   } return $childs;
}

function ebnf_scan(&$input) {
  global $ebnf_lexemes;
  $i = 0; $n = strlen($input); $m = count($ebnf_lexemes); $tokens = array();
  while ($i < $n) {
    $j = 0;
    while ($j < $m &&
      preg_match("/^{$ebnf_lexemes[$j]['expr']}/", substr($input,$i), $matches)==0) $j++;
    if ($j<$m) {
      if ($ebnf_lexemes[$j]['type']!=EBNF_WHITESPACE_TOKEN)
        $tokens[] = array('type' => $ebnf_lexemes[$j]['type'],
          'value' => $matches[0], 'pos' => $i);
      $i += strlen($matches[0]);
	} else
	  throw new Exception("Invalid token at position: $i");
  } return $tokens;
}


function ebnf_check_token($token, $type, $value) {
  return $token['type']==$type && $token['value']==$value;
}

function ebnf_parse_syntax(&$tokens) {
  $dom = new DOMDocument();
  $syntax = $dom->createElement("syntax");
  $syntax->setAttribute('meta', META);
  $dom->appendChild($syntax);
  $i = 0; $token = $tokens[$i++];
  if ($token['type'] == EBNF_LITERAL_TOKEN) {
    $syntax->setAttribute('title',
      stripcslashes(substr($token['value'], 1, strlen($token['value'])-2 )));
    $token = $tokens[$i++];
  }
  if (!ebnf_check_token($token, EBNF_OPERATOR_TOKEN, '{') )
    throw new Exception("Syntax must start with '{': {$token['pos']}");
  $token = $tokens[$i];
  while ($i < count($tokens) && $token['type'] == EBNF_IDENTIFIER_TOKEN) {
    $syntax->appendChild(ebnf_parse_production($dom, $tokens, $i));
    if ($i<count($tokens)) $token = $tokens[$i];
  } $i++; if (!ebnf_check_token($token, EBNF_OPERATOR_TOKEN, '}'))
    throw new Exception("Syntax must end with '}': ".$tokens[count($tokens)-1]['pos']);
  if ($i<count($tokens)) {
    $token = $tokens[$i];
    if ($token['type'] == EBNF_LITERAL_TOKEN) {
      $syntax->setAttribute('meta',
        stripcslashes(substr($token['value'], 1, strlen($token['value'])-2 )));
    }
  }
    return $dom;
}

function ebnf_parse_production(&$dom, &$tokens, &$i) {
  $token = $tokens[$i++];
  if ($token['type']!=EBNF_IDENTIFIER_TOKEN)
    throw new Exception("Production must start with an identifier'{': {$token['pos']}");
  $production = $dom->createElement("rule");
  $production->setAttribute('name', $token['value']);
  $token = $tokens[$i++];
  if (!ebnf_check_token($token, EBNF_OPERATOR_TOKEN, "="))
    throw new Exception("Identifier must be followed by '=': {$token['pos']}");
  $production->appendChild( ebnf_parse_expression($dom, $tokens, $i));
  $token = $tokens[$i++];
  if (!ebnf_check_token($token, EBNF_OPERATOR_TOKEN, '.')
    && !ebnf_check_token($token, EBNF_OPERATOR_TOKEN, ';'))
    throw new Exception("Rule must end with '.' or ';' : {$token['pos']}");
  return $production;
}

function ebnf_parse_expression(&$dom, &$tokens, &$i) {
  $choise = $dom->createElement("choise");
  $choise->appendChild(ebnf_parse_term($dom, $tokens, $i));
  $token=$tokens[$i]; $mul = false;
  while (ebnf_check_token($token, EBNF_OPERATOR_TOKEN, '|')) {
    $i++;
    $choise->appendChild(ebnf_parse_term($dom, $tokens, $i));
    $token=$tokens[$i]; $mul = true;
  } return $mul ? $choise : $choise->removeChild($choise->firstChild);
}

function ebnf_parse_term(&$dom, &$tokens, &$i) {
  $sequence = $dom->createElement("sequence");
  $factor = ebnf_parse_factor($dom, $tokens, $i);
  $sequence->appendChild($factor);
  $token=$tokens[$i]; $mul = false;
  while ($token['value']!='.' && $token['value']!='=' && $token['value']!='|'
    && $token['value']!=')' && $token['value']!=']' && $token['value']!='}') {
    $sequence->appendChild(ebnf_parse_factor($dom, $tokens, $i));
    $token=$tokens[$i]; $mul = true;
  } return $mul ? $sequence: $sequence->removeChild($sequence->firstChild);
}

function ebnf_parse_factor(&$dom, &$tokens, &$i) {
  $token = $tokens[$i++];
  if ($token['type']==EBNF_IDENTIFIER_TOKEN) {
    $identifier = $dom->createElement("identifier");
    $identifier->setAttribute('value', $token['value']);
    return $identifier;
  } if ($token['type']==EBNF_LITERAL_TOKEN){
    $literal = $dom->createElement("terminal");
    $literal->setAttribute('value', stripcslashes(substr($token['value'], 1, strlen($token['value'])-2 )));
    return $literal;
  } if (ebnf_check_token($token, EBNF_OPERATOR_TOKEN, '(')) {
    $expression = ebnf_parse_expression($dom, $tokens, $i);
    $token = $tokens[$i++];
    if (!ebnf_check_token($token, EBNF_OPERATOR_TOKEN, ')'))
      throw new Exception("Group must end with ')': {$token['pos']}");
    return $expression;
  } if (ebnf_check_token($token, EBNF_OPERATOR_TOKEN, '[')) {
    $option = $dom->createElement("option");
    $option->appendChild(ebnf_parse_expression($dom, $tokens, $i));
    $token = $tokens[$i++];
    if (!ebnf_check_token($token, EBNF_OPERATOR_TOKEN, ']'))
      throw new Exception("Option must end with ']': {$token['pos']}");
    return $option;
  } if (ebnf_check_token($token, EBNF_OPERATOR_TOKEN, '{')) {
    $loop = $dom->createElement("loop");
    $loop->appendChild(ebnf_parse_expression($dom, $tokens, $i));
    $token = $tokens[$i++];
    if (!ebnf_check_token($token, EBNF_OPERATOR_TOKEN, '}'))
      throw new Exception("Loop must end with '}': {$token['pos']}");
    return $loop;
  }
  throw new Exception("Factor expected: {$token['pos']}");
}

?>
