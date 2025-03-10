class StructsController < ApplicationController
  def index
    @title = 'Structures'
    order_dir = ''
    if params[:order_dir] == 'desc'
      order_dir = 'DESC';
    end

    case params[:order]
    when 'Struct/Union'
      order = 'struct.name ' + order_dir + ', src_file ' + order_dir
    else
      order = 'src_file ' + order_dir + ', struct.begLine ' + order_dir
    end

    @structs = MyStruct
    if params[:nomacro] == '1'
      @structs = @structs.nomacro
    end
    if params[:nopacked] == '1'
      @structs = @structs.nopacked
    end
    unless params[:filter_struct].blank?
      @structs = @structs.where("struct.name LIKE ? ESCAPE '\\'", params[:filter_struct])
    end
    unless params[:filter_file].blank?
      @structs = @structs.where("source.src LIKE ? ESCAPE '\\'", params[:filter_file])
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
    @title = @struct.name + (@struct.type == 's' ? ' (struct)' : ' (union)')
    base = Member.left_joins(:struct).select('member.id', 'member.struct', 'struct.src', 'member.begLine', 'member.begCol', '0').where(:struct => params[:id])
    #recursive =
    @members = Member.find_by_sql(<<SQL
      WITH RECURSIVE nested(id, struct, src, begLine, begCol, level) AS (
        #{base.to_sql}
        UNION
        SELECT member.id, member.struct, struct.src, member.begLine, member.begCol, level + 1 FROM nested INNER JOIN struct ON struct.id != nested.struct AND struct.src = nested.src AND struct.begLine = nested.begLine AND struct.begCol = nested.begCol LEFT JOIN member ON struct.id = member.struct WHERE level < 10)
      SELECT level, run.version, member.*, member.struct AS struct_id FROM nested NATURAL JOIN member LEFT JOIN run ON member.run = run.id ORDER BY begLine, begCol;
SQL
    )

    respond_to do |format|
      format.html
    end
  end

end
