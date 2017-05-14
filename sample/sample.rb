#!/usr/local/bin/ruby

require 'cdb'

CDB.create('cdb', 'tmp', 0600) do |data|
  data['eins'] = 'ichi'
  data['zwei'] = 'ni'
  data['drei'] = 'san'
  data['vier'] = 'yon'
end

print CDB.dump('cdb'), "\n"


CDB.update('cdb', 'tmp') do |read, write|
  write['one'] = read['eins']
  write['two'] = read['zwei']
  write['three'] = read['drei']
  write['four'] = read['vier']
end

print CDB.dump('cdb'), "\n"


CDB.edit('cdb', 'tmp') do |data|
  data['five'] = 'go'
  data['six'] = 'roku'
  data['seven'] = 'nana'
  data['eight'] = 'hachi'
  data.delete_if { |a,| /t/.match(a) }
end

print CDB.dump('cdb'), "\n"


