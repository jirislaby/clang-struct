class Member < ApplicationRecord
  self.table_name = 'member';
  has_many :use, :foreign_key => 'member'
  belongs_to :struct, :class_name => 'MyStruct', :foreign_key => 'struct'

  scope :unused, -> { where('member.id NOT IN (SELECT member FROM use)').where.not(:name => '<unnamed>') }
end
