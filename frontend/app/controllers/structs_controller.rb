class StructsController < ApplicationController
  def index
    order_dir = ''
    if params[:order_dir] == 'desc'
      order_dir = 'DESC';
    end

    case params[:order]
    when 'Struct'
      order = 'struct.name ' + order_dir + ', src_file ' + order_dir
    else
      order = 'src_file ' + order_dir + ', struct.begLine ' + order_dir
    end

    @structs = MyStruct
    if params[:nopacked] == '1'
      @structs = @structs.nopacked
    end
    unless params[:filter_struct].blank?
      filter = "%#{params[:filter_struct]}%"
      @structs = @structs.where('struct.name LIKE ?', filter)
    end
    unless params[:filter_file].blank?
      filter = "%#{params[:filter_file]}%"
      @structs = @structs.where('source.src LIKE ?', filter)
    end
    @structs = @structs.left_joins(:source)
    @structs = @structs.left_joins(:run)
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
    @structs = @structs.select('struct.*', 'source.src AS src_file', 'run.version').
      order(order).limit(listing_limit)

    respond_to do |format|
      format.html
    end
  end

  def show
    @struct = MyStruct.left_joins(:source).select('struct.*', 'source.src AS src_file').find(params[:id])
    @members = Member.left_joins(:run).select('member.*', 'run.version',
                             "(SELECT id FROM struct AS nested " <<
                             "WHERE nested.src = #{@struct.src} AND " <<
                             "member.begLine == nested.begLine AND " <<
                             "member.begCol == nested.begCol LIMIT 1) " <<
                             "AS nested_id").
           where(struct: @struct).order('member.begLine')

    respond_to do |format|
      format.html
    end
  end

end
