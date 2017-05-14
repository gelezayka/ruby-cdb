require 'cdb.so'

class CDB
  def CDB.each(file, key = nil)
    CDB.open(file) do |db|
      if key
        db.each(key) do |d|
          yield d
        end
      else
        db.each do |k, d|
          yield k, d
        end
      end
    end
  end

  def CDB.create(file, tmp, mode = 0644)
    CDBMake.open(tmp, mode) do |cm|
      yield cm
    end
    File.rename(tmp, file)
  end

  def CDB.edit(file, tmp, mode = nil)
    mode ||= File.stat(file).mode
    CDB.open(file) do |c|
      ary = c.to_ary
      yield ary
      CDBMake.open(tmp, mode) do |cm|
        ary.each do |k, d|
          cm.add(k, d)
        end
      end
    end
    File.rename(tmp, file)
  end

  def CDB.update(file, tmp, mode = nil)
    mode ||= File.stat(file).mode
    CDB.open(file) do |c|
      CDBMake.open(tmp, mode) do |cm|
        yield(c, cm)
      end
    end
    File.rename(tmp, file)
  end

  def CDB.dump(file)
    CDB.open(file) do |c|
      str = %q()
      c.each do |k, d|
        str << %(+#{k.length},#{d.length}:#{k}->#{d}\n)
      end
      return str
    end
  end

  def to_hash
    hash = Hash.new
    each do |k, d|
      hash[k] = d
    end
    return hash
  end

  def to_ary
    ary = CDBEditable.new
    each do |k, d|
      ary.push [k, d]
    end
    return ary
  end
end



class CDBEditable < Array
  def [](key)
    assoc(key)[1]
  end

  def []=(key, data)
    push [key, data]
  end

  alias_method(:add, :[]=)
end


