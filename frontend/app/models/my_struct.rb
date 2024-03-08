class MyStruct < ApplicationRecord
  self.inheritance_column = :type_for_ror
  # MyStruct vs reserved Struct
  def self.model_name
    ActiveModel::Name.new(self, nil, "struct")
  end
  self.table_name = 'struct'

  has_many :member, -> { order('begLine, begCol') }, :foreign_key => 'struct'
  belongs_to :source, :foreign_key => 'src'

  scope :nopacked, -> { where(:packed => 0) }
end
