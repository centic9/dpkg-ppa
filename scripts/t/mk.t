#!/usr/bin/perl
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

use strict;
use warnings;

use Test::More tests => 5;
use File::Spec::Functions qw(rel2abs);

use Dpkg::ErrorHandling;
use Dpkg::IPC;
use Dpkg::Vendor;

my $srcdir = $ENV{srcdir} || '.';
my $datadir = "$srcdir/t/mk";

# Turn these into absolute names so that we can safely switch to the test
# directory with «make -C».
$ENV{$_} = rel2abs($ENV{$_}) foreach qw(srcdir DPKG_DATADIR DPKG_ORIGINS_DIR);

# Delete variables that can affect the tests.
delete $ENV{DEB_VENDOR};

sub test_makefile {
    my $makefile = shift;

    spawn(exec => [ 'make', '-C', $datadir, '-f', $makefile ],
          wait_child => 1, nocheck => 1);
    ok($? == 0, "makefile $makefile computes all values correctly");
}

sub get_arch_vars {
    my $prefix = shift // '';
    my %arch;

    open my $arch_env, '-|', "$srcdir/dpkg-architecture.pl", '-f'
        or subprocerr('dpkg-architecture');
    while (<$arch_env>) {
        chomp;
        my ($key, $value) = split /=/, $_, 2;
        $arch{$key} = $value;
    }
    close $arch_env or subprocerr('dpkg-architecture');

    return %arch;
}

# Test makefiles.

my %arch = get_arch_vars();

delete $ENV{$_} foreach keys %arch;
$ENV{"TEST_$_"} = $arch{$_} foreach keys %arch;
test_makefile('architecture.mk');
$ENV{$_} = $arch{$_} foreach keys %arch;
test_makefile('architecture.mk');

test_makefile('buildflags.mk');

test_makefile('pkg-info.mk');

test_makefile('vendor.mk');

1;
