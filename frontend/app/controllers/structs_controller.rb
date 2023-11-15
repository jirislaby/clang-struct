class StructsController < ApplicationController
  def index
    @structs = MyStruct
    unless params[:filter].blank?
      @filter = "%#{params[:filter]}%"
      @structs = @structs.where('struct.name LIKE ?', @filter)
    end
    unless params[:filter_file].blank?
      @filter = "%#{params[:filter_file]}%"
      @structs = @structs.where('source.src LIKE ?', @filter)
    end
    @structs = @structs.left_joins(:source)
    @structs_all_count = @structs.count # ALL COUNT

    @page = @offset = 0
    unless params[:page].blank?
      @page = params[:page].to_i
      @offset = @page * listing_limit
      @structs = @structs.offset(@offset)
    end
    @structs = @structs.limit(listing_limit)
    @structs_count = @structs.count # COUNT

    if @structs_all_count > @offset + listing_limit
      @next_page = @page + 1
    else
      @next_page = 0
    end
    @structs = @structs.select('struct.*', 'source.src AS src_file').
      order('src_file, struct.begLine').limit(listing_limit)

    respond_to do |format|
      format.html
    end
  end

  def show
    @struct = MyStruct.left_joins(:source).select('struct.*', 'source.src AS src_file').find(params[:id])
    @members = Member.select('member.*',
                             "(SELECT id FROM struct AS nested " <<
                             "WHERE nested.src = #{@struct.src} AND " <<
                             "member.begLine == nested.begLine AND " <<
                             "member.begCol == nested.begCol LIMIT 1) " <<
                             "AS nested_id").
      where(struct: @struct)

    respond_to do |format|
      format.html
    end
  end

end
