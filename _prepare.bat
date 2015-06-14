CALL vulcanize --inline-css --inline-scripts --strip-comments _includes\polymer.html >_includes\polymer-vulcanized.temp.html

echo {%%raw%%} >_includes\polymer-vulcanized.html
type _includes\polymer-vulcanized.temp.html >>_includes\polymer-vulcanized.html
echo {%%endraw%%} >>_includes\polymer-vulcanized.html
del _includes\polymer-vulcanized.temp.html
