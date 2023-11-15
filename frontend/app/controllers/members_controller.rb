class MembersController < ApplicationController
  def index
    @members = Member
    if params[:unused] == '1'
      @members = @members.unused
    end
    unless params[:filter].blank?
      @filter = "%#{params[:filter]}%"
      @members = @members.where('member.name LIKE ? OR struct.name LIKE ?',
                                @filter, @filter)
    end
    unless params[:filter_file].blank?
      @filter = "%#{params[:filter_file]}%"
      @members = @members.where('source.src LIKE ?', @filter)
    end
    @members = @members.left_joins({:struct => :source})
    @members_all_count = @members.count # ALL COUNT

    @page = @offset = 0
    unless params[:page].blank?
      @page = params[:page].to_i
      @offset = @page * listing_limit
      @members = @members.offset(@offset)
    end
    @members = @members.limit(listing_limit)
    @members_count = @members.count # COUNT

    if @members_all_count > @offset + listing_limit
      @next_page = @page + 1
    else
      @next_page = 0
    end

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
