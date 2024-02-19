class Member < ApplicationRecord
  self.table_name = 'member';
  has_many :use, :foreign_key => 'member'
  belongs_to :struct, :class_name => 'MyStruct', :foreign_key => 'struct'

  scope :unused, -> { where(:uses => 0).where.not(:name => '<unnamed>') }
  scope :noimplicit, -> { where('member.uses = member.implicit_uses').where.not(:name => '<unnamed>') }
end
