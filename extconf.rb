require 'mkmf'

CONFIG['CC'] = 'clang'
CONFIG['CXX'] = 'clang'
$LOCAL_LIBS += ['cdb.a', 'alloc.a', 'error.o', 'seek_set.o', 'byte_copy.o',
                'byte_diff.o', 'buffer_get.o', 'buffer_put.o', 'str_len.o',
                'byte_cr.o', 'buffer.o', 'uint32_pack.o', 'uint32_unpack.o'].
                map { |s| 'cdb/' + s }.join(' ')
create_makefile('cdb')

