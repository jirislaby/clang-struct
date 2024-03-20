class Run < ApplicationRecord
  self.table_name = 'run'

  has_many :source, :foreign_key => 'run'
  has_many :struct, :foreign_key => 'run'
  has_many :use, :foreign_key => 'run'
end
