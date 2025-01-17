class Use < ApplicationRecord
  self.table_name = 'use'
  belongs_to :run, :foreign_key => 'run'
  belongs_to :member, :foreign_key => 'member'
  belongs_to :source, :foreign_key => 'src'

  scope :noimplicit, -> { where(:implicit => 0) }
  scope :onlyload, -> { where(:load => 1) }
  scope :onlystore, -> { where(:load => 0) }
  scope :onlyunknown, -> { where(:load => nil) }
end
