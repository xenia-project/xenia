* [Install Ruby 2.2+ and DevKit](http://rubyinstaller.org/downloads).
* `gem update --system`
* Extract DevKit
* `ruby dk.rb init`
* `ruby dk.rb install`
* `gem install bundler`
* `npm install -g bower vulcanize`

From the repository root:

```
bundle install
bower install
bundle exec jekyll serve
```

If changing anything in `polymer.html`, run `_prepare.bat`.

Icons from: https://www.google.com/design/icons/
