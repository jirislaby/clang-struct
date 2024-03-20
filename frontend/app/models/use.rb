class Use < ApplicationRecord
  self.table_name = 'use'
  belongs_to :run, :foreign_key => 'run'
  belongs_to :member, :foreign_key => 'member'
  belongs_to :source, :foreign_key => 'src'
end
