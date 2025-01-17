class UsesController < ApplicationController
#  def index
#    @uses = Use.joins(:member).limit(100);
#
#    respond_to do |format|
#      format.html
#    end
#  end

  def show
    @member = Member.joins(:struct).find(params[:id])
    @uses = Use.where(member: @member).left_joins(:run)

    case params[:access]
    when 'load'
      @uses = @uses.onlyload
    when 'store'
      @uses = @uses.onlystore
    when 'unknown'
      @uses = @uses.onlyunknown
    end

    if params[:noimplicit] == '1'
      @uses = @uses.noimplicit
    end

    listing_limit_cropped = listing_limit * 3 + 1

    @page = @offset = 0
    unless params[:page].blank?
      @page = params[:page].to_i
      @offset = @page * listing_limit
      @uses = @uses.offset(@offset)
    end
    @uses = @uses.limit(listing_limit_cropped)
    @uses_all_count = @uses.count # ALL COUNT
    @uses = @uses.limit(listing_limit)
    @uses_count = @uses.count # COUNT

    @uses_all_count += @offset
    if @uses_all_count > @offset + listing_limit
      if @uses_all_count >= @offset + listing_limit_cropped
        @uses_all_count = "many"
      end
      @next_page = @page + 1
    else
      @next_page = 0
    end

    @uses = @uses.joins(:source).
      select('use.*', 'run.version', 'source.src AS src_file').order('src_file, begLine')

    respond_to do |format|
      format.html
    end
  end

end
