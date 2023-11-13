class MembersController < ApplicationController
  def index
    @members = Member
    if params[:unused] == '1'
      @members = @members.unused
    end
    if params[:filter] != ''
      @filter = "%#{params[:filter]}%"
      @members = @members.where('member.name LIKE ? OR struct.name LIKE ?',
                                @filter, @filter)
    end
    if params[:filter_file] != ''
      @filter = "%#{params[:filter_file]}%"
      @members = @members.where('source.src LIKE ?', @filter)
    end
    @members = @members.joins({:struct => :source}).limit(500)
    @members_count = @members.count
    @members = @members.select('member.*', 'member.struct AS struct_id',
                               'struct.name AS struct_name',
                               'struct.begLine AS struct_begLine',
                               'source.src AS src_file').
      order('struct.name, member.begLine')

    respond_to do |format|
      format.html
    end
  end

end
