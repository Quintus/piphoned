require "tempfile"
require "rake/clean"

str = File.read("CMakeLists.txt")

VERSION_MAJOR = str.match(/PIPHONED_VERSION_MAJOR (\d+)/)[1]
VERSION_MINOR = str.match(/PIPHONED_VERSION_MINOR (\d+)/)[1]
VERSION_PATCH = str.match(/PIPHONED_VERSION_PATCH (\d+)/)[1]

THIS_DIR = File.expand_path(File.dirname(__FILE__))

CLOBBER.include("build")

desc "Create a release tarball."
task :tarball => [:clobber] do
  version = "#{VERSION_MAJOR}.#{VERSION_MINOR}.#{VERSION_PATCH}"
  puts "piphoned version is #{version}"

  Dir.mktmpdir do |tmpdir|
    cd tmpdir do
      ln_s THIS_DIR, "piphoned-#{version}"
      sh "tar -cvjh --exclude=.git -f piphoned-#{version}.tar.bz2 piphoned-#{version}"
      rm "piphoned-#{version}"
      mv "piphoned-#{version}.tar.bz2", "#{THIS_DIR}/piphoned-#{version}.tar.bz2"
    end
  end
end
