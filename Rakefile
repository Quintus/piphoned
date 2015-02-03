require "tempfile"
require "rake/clean"

str = File.read("CMakeLists.txt")

VERSION_MAJOR = str.match(/PIPHONED_VERSION_MAJOR (\d+)/)[1]
VERSION_MINOR = str.match(/PIPHONED_VERSION_MINOR (\d+)/)[1]
VERSION_PATCH = str.match(/PIPHONED_VERSION_PATCH (\d+)/)[1]
VERSION       = "#{VERSION_MAJOR}.#{VERSION_MINOR}.#{VERSION_PATCH}"

THIS_DIR = File.expand_path(File.dirname(__FILE__))

CLOBBER.include("build")

file "piphoned_#{VERSION}.tar.bz2" do
  puts "piphoned version is #{VERSION}"

  Dir.mktmpdir do |tmpdir|
    cd tmpdir do
      ln_s THIS_DIR, "piphoned-#{VERSION}"
      sh "tar -cvjh --exclude=.git -f piphoned_#{VERSION}.tar.bz2 piphoned-#{VERSION}"
      rm "piphoned-#{VERSION}"
      mv "piphoned_#{VERSION}.tar.bz2", "#{THIS_DIR}/piphoned_#{VERSION}.tar.bz2"
    end
  end
end

desc "Create a release tarball."
task :tarball => [:clobber, "piphoned_#{VERSION}.tar.bz2"]

desc "Build a .deb package."
task :debpackage => [:tarball] do
end
