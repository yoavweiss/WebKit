#! /usr/bin/env perl

# Copyright (C) 2010-2011 Google Inc. All rights reserved.
# Copyright (C) 2025 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#    # Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#    # Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#    # Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

my $arrayName = shift;
my $inName = shift;
my $outName = shift;

open(my $input, '<', $inName) or die "Can't open file for read: $inName $!";
$/ = undef;
my $text = <$input>;
close($input);

my @values = map ('0x' . unpack("H*", $_), split(undef, $text));
my $size = @values;
my $array = join(', ', @values);

open(my $output, '>', $outName) or die "Can't open file for write: $outName $!";
print $output "#ifdef __cplusplus\n#include <array>\n#include <wtf/text/LChar.h>\nstatic constexpr std::array<Latin1Character, $size> $arrayName\n#else\nstatic const unsigned char ${arrayName}[] =\n#endif\n{\n$array\n};\n";
close($output);
