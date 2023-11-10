class Source < ApplicationRecord
  self.table_name = 'source'
  has_many :struct, :foreign_key => 'src'
  has_many :use, :foreign_key => 'src'
end
