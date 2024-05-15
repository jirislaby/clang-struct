class MembersController < ApplicationController
  def index
    order_dir = ''
    if params[:order_dir] == 'desc'
      order_dir = 'DESC';
    end

    case params[:order]
    when 'Member'
      order = 'member.name ' + order_dir + ', struct.name ' + order_dir
    when 'File'
      order = 'src_file ' + order_dir + ', member.begLine ' + order_dir
    when 'Uses'
      order = 'member.uses ' + order_dir + ', member.name ' + order_dir
    else
      order = 'struct.name ' + order_dir + ', member.begLine ' + order_dir
    end

    @members = Member
    if params[:unused] == '1'
      if params[:noimplicit] == '1'
        @members = @members.noimplicit
      else
        @members = @members.unused
      end
    end
    unless params[:filter_struct].blank?
      @members = @members.where("struct.name LIKE ? ESCAPE '\\'", params[:filter_struct])
    end
    unless params[:filter_member].blank?
      @members = @members.where("member.name LIKE ? ESCAPE '\\'", params[:filter_member])
    end
    if params[:noreserved] == '1'
      @members = @members.where('member.name NOT LIKE ? AND ' +
                                'member.name NOT LIKE ? AND ' +
                                'member.name NOT LIKE ? AND ' +
                                'member.name NOT LIKE ? AND ' +
                                'struct.name NOT LIKE ? AND ' +
                                'struct.name NOT LIKE ?',
                                '%dummy%', '%pad%', '%reserve%',
                                '%unused%',
                                'compat_%', 'trace_event_raw_%')
    end
    unless params[:filter_file].blank?
      @members = @members.where("source.src LIKE ? ESCAPE '\\'", params[:filter_file])
    end
    @members = @members.left_joins({:struct => :source}).left_joins(:run)
    if params[:nopacked] == '1'
      @members = @members.merge(MyStruct.nopacked)
    end

    listing_limit_cropped = listing_limit * 3 + 1

    @page = @offset = 0
    unless params[:page].blank?
      @page = params[:page].to_i
      @offset = @page * listing_limit
      @members = @members.offset(@offset)
    end
    @members = @members.limit(listing_limit_cropped)
    @members_all_count = @members.count # ALL COUNT

    @members = @members.limit(listing_limit)
    @members_count = @members.count # COUNT

    @members_all_count += @offset
    if @members_all_count > @offset + listing_limit
      if @members_all_count >= @offset + listing_limit_cropped
        @members_all_count = "many"
      end
      @next_page = @page + 1
    else
      @next_page = 0
    end

    @members = @members.select('member.*', 'member.struct AS struct_id',
                               'run.version',
                               'struct.type AS struct_type',
                               'struct.name AS struct_name',
                               'struct.begLine AS struct_begLine',
                               'source.src AS src_file').
      order(order)

    respond_to do |format|
      format.html
    end
  end

end
