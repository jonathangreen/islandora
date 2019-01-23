#!/bin/bash

# Common checks to get run during the 'script' section in Travis.
OUTPUT=0

# Make OUTPUT equal return code if return code is not 0
function checkReturn {
  if [ $1 -ne 0 ]; then
    OUTPUT=$1
  fi
}  

# Lint
find $TRAVIS_BUILD_DIR -type f \( -name '*.php' -o -name '*.inc' -o -name '*.module' -o -name '*.install' -o -name '*.test' \) -exec php -l {} \;
checkReturn $?

# Check line endings
$ISLANDORA_DIR/tests/scripts/line_endings.sh $TRAVIS_BUILD_DIR
checkReturn $?

# Coding standards
drush coder-review --reviews=production,security,style,i18n,potx $TRAVIS_BUILD_DIR
checkReturn $?

# Code sniffer
if [ -d "$HOME/.composer/vendor/drupal/coder/coder_sniffer/Drupal" ]; then
  DRUPAL_SNIFFS=$HOME/.composer/vendor/drupal/coder/coder_sniffer/Drupal
elif [ -d "$HOME/.config/composer/vendor/drupal/coder/coder_sniffer/Drupal" ]; then
  DRUPAL_SNIFFS=$HOME/.config/composer/vendor/drupal/coder/coder_sniffer/Drupal
else
  DRUPAL_SNIFFS="Drupal"
fi
/usr/bin/phpcs --standard=$DRUPAL_SNIFFS --extensions="php,module,inc" --ignore="*.md" $TRAVIS_BUILD_DIR
checkReturn $?

# Copy/paste detection
phpcpd --names *.module,*.inc,*.test $TRAVIS_BUILD_DIR
checkReturn $?

exit $OUTPUT
