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
    @uses_count = @uses.count
    @uses = @uses.joins(:source).
      select('use.*', 'run.version', 'source.src AS src_file').order('src_file, begLine')

    respond_to do |format|
      format.html
    end
  end

end
